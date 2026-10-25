#include "app/console.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "core/tunables.hpp"
#include "theme/themefile.hpp"
#include "world/world.hpp"
#include "world/worldfile.hpp"

namespace wm {


namespace {

// ---------------------------------------------------------------------------
// Walker variables (set / get / reset)
// ---------------------------------------------------------------------------

struct VarDesc {
    const char* name;
    float*      ptr;
    float       def;
    float       min;
    float       max;
    const char* help;
};

const VarDesc kVars[] = {
    {"walk_speed",      &T.walkSpeed,      6.0f,   0.0f,   500.0f, "ground move speed (units/s)"},
    {"fly_speed",       &T.flySpeed,      50.0f,   0.0f,   500.0f, "flight speed (units/s)"},
    {"sprint_mult",     &T.sprintMult,     4.0f,   1.0f,    20.0f, "Shift speed multiplier"},
    {"gravity",         &T.gravity,      -24.0f, -200.0f,    0.0f, "gravity acceleration"},
    {"jump_velocity",   &T.jumpVelocity,   8.5f,   0.0f,   100.0f, "initial jump speed"},
    {"eye_height",      &T.eyeHeight,      1.7f,   0.0f,    10.0f, "camera height above feet"},
    {"mouse_sens",      &T.mouseSens,      0.12f,  0.001f,   5.0f, "degrees per mouse pixel"},
    {"double_tap_time", &T.doubleTapTime,  0.30f,  0.05f,    1.0f, "Space double-tap window (s)"},
    {"grid_radius",     &T.gridRadius,    80.0f,   5.0f,   500.0f, "grid draw distance (no world loaded)"},
    {"major_every",     &T.majorEvery,    10.0f,   1.0f,   100.0f, "bright line every N cells"},
    {"fov",             &T.fov,           90.0f,  20.0f,   150.0f, "vertical field of view (deg)"},
    {"view_distance",   &T.viewDistance, 8000.0f, 32.0f, 10000.0f, "terrain draw distance (units)"},
    {"lod_distance",    &T.lodDistance, 500.0f,  20.0f,   800.0f, "closer than this uses full detail"},
};

const VarDesc* FindVar(const std::string& name) {
    for (const VarDesc& v : kVars)
        if (name == v.name) return &v;
    return nullptr;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string FormatVarLine(const VarDesc& v, bool full) {
    char buf[320];
    if (full) {
        std::snprintf(buf, sizeof buf, "  %-16s %-9g default %-7g range %g..%g  %s",
                      v.name, *v.ptr, v.def, v.min, v.max, v.help);
    } else {
        std::snprintf(buf, sizeof buf, "  %-16s %-9g default %g", v.name, *v.ptr, v.def);
    }
    return buf;
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::istringstream ss(line);
    std::vector<std::string> out;
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}

// Parses a whole string as a float. Returns false if anything is left over.
bool ParseFloat(const std::string& txt, float& out) {
    char* end = nullptr;
    out = std::strtof(txt.c_str(), &end);
    return end != txt.c_str() && *end == '\0';
}

// ---------------------------------------------------------------------------
// Theme values (theme list / set / time / reset / savetheme / loadtheme)
// ---------------------------------------------------------------------------

void PrintThemeValue(Console& con, const Look& look, const LookField& f) {
    char buf[256];
    const float* p = LookValue(look, f);
    if (f.count == 1) std::snprintf(buf, sizeof buf, "  %-14s %g", f.name, p[0]);
    else              std::snprintf(buf, sizeof buf, "  %-14s %g %g %g", f.name, p[0], p[1], p[2]);
    con.print(buf);
}

const LookField* FindLookField(const std::string& name) {
    for (const LookField& f : LookFields())
        if (name == f.name) return &f;
    return nullptr;
}

void ThemeCommand(const std::vector<std::string>& args, Console& con, Look& look) {
    if (args.empty() || Lower(args[0]) == "list") {
        con.print("Theme (current values):");
        for (const LookField& f : LookFields()) PrintThemeValue(con, look, f);
        return;
    }

    const std::string sub = Lower(args[0]);

    if (sub == "reset") {
        look = Look{};
        con.print("theme reset to defaults");
        return;
    }

    if (sub == "time") {
        float h = 0.0f;
        if (args.size() < 2 || !ParseFloat(args[1], h)) {
            con.print("! usage: theme time <hour 0-24>");
            return;
        }
        look.clock = std::clamp(h, 0.0f, 24.0f);
        PrintThemeValue(con, look, *FindLookField("clock"));
        return;
    }

    if (sub == "set") {
        if (args.size() < 2) {
            con.print("! usage: theme set <name> <value...>");
            return;
        }
        const LookField* f = FindLookField(Lower(args[1]));
        if (!f) {
            con.print("! unknown theme value: " + args[1]);
            return;
        }
        if (static_cast<int>(args.size()) - 2 != f->count) {
            con.print("! " + std::string(f->name) + " takes " + std::to_string(f->count) + " value(s)");
            return;
        }

        // Parse and check every value before changing anything.
        float vals[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < f->count; ++i) {
            const std::string& txt = args[static_cast<std::size_t>(2 + i)];
            if (!ParseFloat(txt, vals[i])) {
                con.print("! not a number: " + txt);
                return;
            }
            vals[i] = Limit(vals[i], f->lo, f->hi);
        }
        float* dst = LookValue(look, *f);
        for (int i = 0; i < f->count; ++i) dst[i] = vals[i];
        PrintThemeValue(con, look, *f);
        return;
    }

    if (sub == "savetheme" || sub == "loadtheme") {
        con.print("! use: savetheme <name>  or  loadtheme <name>");
        return;
    }

    con.print("! unknown theme command: " + sub + " (try: theme list)");
}

// ---------------------------------------------------------------------------
// Generation and world files
// ---------------------------------------------------------------------------

void GenCommand(const std::vector<std::string>& args, Console& con, Session& s) {
    if (!args.empty() && Lower(args[0]) == "status") {
        if (s.generating) {
            con.print("generating... " + std::to_string(s.progress.load()) + "%");
        } else if (s.hasWorld) {
            const std::string name = s.worldName.empty() ? std::string("unsaved") : s.worldName;
            con.print("world: " + name + ", seed " + std::to_string(s.world.seed) + ", " +
                      std::to_string(s.world.size) + " x " + std::to_string(s.world.size) + " tiles");
        } else {
            con.print("no world yet. try: gen 1");
        }
        return;
    }

    if (s.generating) {
        con.print("! already generating (" + std::to_string(s.progress.load()) + "%)");
        return;
    }
    if (args.size() > 2) {
        con.print("! usage: gen [seed] [size]   or   gen status");
        return;
    }

    // A new random seed unless one is typed.
    GenSettings g = s.lastSettings;
    g.seed = RandomSeed();
    if (args.size() >= 1) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(args[0].c_str(), &end, 10);
        if (end == args[0].c_str() || *end != '\0') {
            con.print("! seed must be a whole number");
            return;
        }
        g.seed = static_cast<uint32_t>(v);
    }
    if (args.size() >= 2) {
        char* end = nullptr;
        const long v = std::strtol(args[1].c_str(), &end, 10);
        if (end == args[1].c_str() || *end != '\0') {
            con.print("! size must be a whole number of tiles");
            return;
        }
        g.size = static_cast<int>(v);
    }

    // Clamp, then round up to a whole number of chunks.
    g.size = std::clamp(g.size, kMinWorldTiles, MaxWorldTiles());
    g.size = (g.size + kChunkTiles - 1) / kChunkTiles * kChunkTiles;

    StartGenerate(s, g);
    con.print("generating seed " + std::to_string(g.seed) + ", " + std::to_string(g.size) + " x " +
              std::to_string(g.size) + " tiles...");
}

void SaveCommand(const std::vector<std::string>& tok, Console& con, Session& s) {
    if (tok.size() < 2) {
        con.print("! usage: save <name>");
        return;
    }
    std::string err;
    if (SaveWorld(s, tok[1], err)) con.print("saved world " + tok[1]);
    else                           con.print("! save failed: " + err);
}

void LoadCommand(const std::vector<std::string>& tok, Console& con, Session& s) {
    if (tok.size() < 2) {
        con.print("! usage: load <name>   (see: worlds)");
        return;
    }
    if (s.generating) {
        con.print("! wait for generation to finish first");
        return;
    }
    std::string err;
    if (LoadWorld(s, tok[1], err)) con.print("loaded world " + tok[1]);
    else                           con.print("! load failed: " + err);
}

void ListNames(Console& con, const std::string& what, const std::vector<std::string>& names) {
    if (names.empty()) {
        con.print("no saved " + what + " yet");
        return;
    }
    con.print(what + ":");
    for (const std::string& n : names) con.print("  " + n);
}

void ThemeFileCommand(const std::vector<std::string>& tok, Console& con, Look& look, const Session& s) {
    if (tok.size() < 2) {
        con.print("! usage: savetheme <name>  or  loadtheme <name>");
        return;
    }
    const std::string path = s.themesDir + "/" + tok[1] + ".json";
    std::string err;
    if (tok[0] == "savetheme") {
        if (!IsSafeName(tok[1])) {
            con.print("! theme name may use letters, digits, space, - and _");
        } else if (SaveLookFile(look, path, err)) {
            con.print("saved theme " + tok[1]);
        } else {
            con.print("! save failed: " + err);
        }
    } else {
        if (LoadLookFile(path, look, err)) con.print("loaded theme " + tok[1]);
        else                               con.print("! load failed: " + err);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void Console::print(const std::string& s) {
    lines.push_back(s);
    while (lines.size() > 300) lines.pop_front();
}

void PrintConsoleHelp(Console& con) {
    con.print("Commands:");
    con.print("  help                 show this help");
    con.print("  list                 list every walker variable with range");
    con.print("  get <var>            show a variable (or just type its name)");
    con.print("  set <var> <value>    change a walker value live");
    con.print("  reset [var]          restore one default, or all of them");
    con.print("  gen [seed] [size]    generate a world (random seed unless given)");
    con.print("  gen status           generation progress or the loaded world");
    con.print("  export <name>        write the loaded world to data/worlds (default name world_<seed>)");
    con.print("  import <name>        read a world back from data/worlds");
    con.print("  worlds               list exported worlds");
    con.print("  theme list           show the sky, sun and fog values");
    con.print("  theme set <v> ...    change a theme value (colors take 3 numbers)");
    con.print("  theme time <hour>    set the time of day");
    con.print("  theme reset          restore the default theme");
    con.print("  savetheme <name>     save the theme to data/themes");
    con.print("  loadtheme <name>     load a saved theme");
    con.print("  clear                clear the console");
    con.print("  quit                 exit the program");
    con.print("Some variables (current, default):");
    static const char* kShown[] = {
        "walk_speed", "fly_speed", "mouse_sens", "view_distance", "lod_distance",
    };
    for (const char* n : kShown)
        if (const VarDesc* v = FindVar(n)) con.print(FormatVarLine(*v, false));
}

void ResetWalkerVars() {
    for (const VarDesc& v : kVars) *v.ptr = v.def;
}

void RunConsoleLine(const std::string& line, Console& con, bool& running, Look& look, Session& s) {
    con.print("> " + line);

    const std::vector<std::string> tok = Tokenize(line);
    if (tok.empty()) return;

    const std::string cmd = Lower(tok[0]);
    const std::string a = tok.size() > 1 ? Lower(tok[1]) : std::string();
    const std::vector<std::string> rest(tok.begin() + (tok.empty() ? 0 : 1), tok.end());

    if (cmd == "help") {
        PrintConsoleHelp(con);
    } else if (cmd == "list") {
        for (const VarDesc& v : kVars) con.print(FormatVarLine(v, true));
    } else if (cmd == "clear") {
        con.lines.clear();
    } else if (cmd == "quit" || cmd == "exit") {
        running = false;
    } else if (cmd == "get") {
        const VarDesc* v = FindVar(a);
        if (!v) con.print("! unknown variable: " + a);
        else    con.print(FormatVarLine(*v, false));
    } else if (cmd == "set") {
        if (tok.size() < 3) {
            con.print("! usage: set <var> <value>");
            return;
        }
        const VarDesc* v = FindVar(a);
        if (!v) {
            con.print("! unknown variable: " + a);
            return;
        }
        float val = 0.0f;
        if (!ParseFloat(tok[2], val)) {
            con.print("! not a number: " + tok[2]);
            return;
        }
        const float clamped = Limit(val, v->min, v->max);
        *v->ptr = clamped;

        std::string msg = FormatVarLine(*v, false);
        if (clamped != val) msg += "  (clamped to range)";
        con.print(msg);
    } else if (cmd == "reset") {
        if (a.empty()) {
            ResetWalkerVars();
            con.print("all variables reset to defaults");
        } else if (const VarDesc* v = FindVar(a)) {
            *v->ptr = v->def;
            con.print(FormatVarLine(*v, false));
        } else {
            con.print("! unknown variable: " + a);
        }
    } else if (cmd == "gen") {
        GenCommand(rest, con, s);
    } else if (cmd == "export" || cmd == "save") {
        SaveCommand(tok, con, s);
    } else if (cmd == "import" || cmd == "load") {
        LoadCommand(tok, con, s);
    } else if (cmd == "worlds") {
        ListNames(con, "saved worlds", ListWorldNames(s.worldsDir));
    } else if (cmd == "themes") {
        ListNames(con, "saved themes", ListLookNames(s.themesDir));
    } else if (cmd == "savetheme" || cmd == "loadtheme") {
        ThemeFileCommand(tok, con, look, s);
    } else if (cmd == "theme") {
        ThemeCommand(rest, con, look);
    } else if (FindVar(cmd)) {
        con.print(FormatVarLine(*FindVar(cmd), false));
    } else {
        con.print("! unknown command: " + cmd + " (type help)");
    }
}

// Cuts a line to fit maxW pixels, keeping the start of the string.
namespace {
std::string FitStart(const Renderer& r, std::string s, float maxW) {
    if (!gOpt.cacheText) {
        while (!s.empty() && r.MeasureText(s) > maxW) s.pop_back();
        return s;
    }
    // Binary search for the longest prefix that fits. The width is monotonic in length.
    std::size_t lo = 0, hi = s.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (r.MeasureText(s.substr(0, mid)) <= maxW) lo = mid;
        else                                          hi = mid - 1;
    }
    s.resize(lo);
    return s;
}

// Cuts a line to fit maxW pixels, keeping the end of the string (for the prompt).
std::string FitEnd(const Renderer& r, std::string s, float maxW) {
    while (!s.empty() && r.MeasureText(s) > maxW) s.erase(0, 1);
    return s;
}
}  // namespace

void DrawConsole(Renderer& r, const Console& con, int w, int h) {
    const float pad = 10.0f;
    const float lineH = r.LineHeight() + 4.0f;
    const float panelH = h * 0.45f;
    const float promptY = panelH - r.LineHeight() - 10.0f;
    const float maxW = static_cast<float>(w) - 2.0f * pad;

    r.Rect(0.0f, 0.0f, static_cast<float>(w), panelH, V3{0.0f, 0.0f, 0.0f}, 0.85f);
    r.Rect(0.0f, panelH - 2.0f, static_cast<float>(w), 2.0f, V3{0.0f, 0.86f, 1.0f}, 1.0f);

    // Output history, newest closest to the prompt.
    float y = promptY - lineH;
    for (int i = static_cast<int>(con.lines.size()) - 1; i >= 0 && y >= 0.0f; --i, y -= lineH) {
        const std::string s = FitStart(r, con.lines[static_cast<std::size_t>(i)], maxW);
        const bool err = !s.empty() && s[0] == '!';
        if (err) r.Text(pad, y, s, V3{1.0f, 0.55f, 0.2f});
        else     r.Text(pad, y, s, V3{0.80f, 0.85f, 0.90f});
    }

    // Prompt with blinking cursor.
    std::string prompt = "> " + con.input;
    if ((SDL_GetTicks() / 500) % 2 == 0) prompt += "_";
    r.Text(pad, promptY, FitEnd(r, prompt, maxW), V3{0.0f, 0.86f, 1.0f});
}

}  // namespace wm
