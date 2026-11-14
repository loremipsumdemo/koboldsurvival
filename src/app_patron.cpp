#include "app.h"
#include "patrons.h"

using namespace play;

// Patrons (patrons.h): the Patron Dialogue banner at the top middle of the screen, and the window of all of them (See Patron on an Altar's
// menu, and the Patron button of the party screen, where yours is picked).

namespace {
constexpr Uint64 CHAT_STEP_MS = 4000;     // a line has been up this long before the next comes, and the banner fades this long after the last
constexpr double CHAT_SAME_SECONDS = 3600.0;   // game time: the same event between the same two factions (either way round) is spoken of once an hour
constexpr size_t CHAT_KEEP = 10;          // the lines waiting to be shown (more than that and the oldest are let go)
constexpr size_t PATRON_KEEP_MESSAGES = 500;   // the messages remembered, for the Patrons tab
constexpr int CHAT_SHOWN = 2;             // and shown at a time

struct PatronLayout {
    Rect box, title, close, random;
    std::vector<Rect> cells;
};

PatronLayout layoutPatron(int W, int H, int count, bool pick) {
    const float s = uiScaleFor((float)H);
    const int cols = 4, rows = std::max(1, (count + cols - 1) / cols);
    const float pad = 14.0f * s, gap = 8.0f * s, line = 30.0f * s, btnH = 34.0f * s;
    const float fixedH = pad + line + gap + (pick ? gap + btnH : 0.0f) + pad;
    const float cell = std::min(96.0f * s, std::max(32.0f * s, ((float)H - 20.0f * s - fixedH - (rows - 1) * gap) / rows));
    PatronLayout L;
    const float w = cols * cell + (cols - 1) * gap + 2.0f * pad;
    const float h = fixedH + rows * cell + (rows - 1) * gap;
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.box.x + pad, L.box.y + pad, w - 2.0f * pad - line, line };
    L.close = Rect{ L.box.x + w - pad - line, L.box.y + pad, line, line };
    const float top = L.title.y + line + gap;
    for (int i = 0; i < count; ++i) {
        L.cells.push_back(Rect{ L.box.x + pad + (float)(i % cols) * (cell + gap), top + (float)(i / cols) * (cell + gap), cell, cell });
    }
    if (pick) L.random = Rect{ L.box.x + pad, L.box.y + h - pad - btnH, w - 2.0f * pad, btnH };
    return L;
}
}

std::string App::patronButtonText() {
    const std::vector<PatronInfo>& list = patrons();
    return langf("UILABEL_PATRON_BUTTON", { patronPick >= 0 && (size_t)patronPick < list.size() ? lang(list[(size_t)patronPick].nameKey) : lang("UILABEL_PATRON_RANDOM") });
}

void App::openPatrons(int faction, bool pick) {
    patronOpen = true;
    patronPicking = pick;
    patronFaction = faction;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

bool App::pressOnPatrons() {
    if (!patronOpen || (screen != Screen::Playing && screen != Screen::PartySetup)) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const PatronLayout L = layoutPatron(winW, winH, (int)patrons().size(), patronPicking);
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        patronOpen = false;
        playSfx(Sfx::UiBack);
        return true;
    }
    if (patronPicking) {
        if (L.random.contains(x, y)) {
            patronPick = -1;
            patronOpen = false;
            playSfx(Sfx::UiClick);
            return true;
        }
        for (size_t i = 0; i < L.cells.size(); ++i) {
            if (!L.cells[i].contains(x, y)) continue;
            patronPick = (int)i;
            patronOpen = false;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    return true;
}

void App::drawPatrons() {
    if (!patronOpen || (screen != Screen::Playing && screen != Screen::PartySetup)) return;
    const std::vector<PatronInfo>& list = patrons();
    const PatronLayout L = layoutPatron(winW, winH, (int)list.size(), patronPicking);
    const float s = uiScaleFor((float)winH), textH = renderer.listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dim{ 0.80f, 0.72f, 0.64f, 1.0f };
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_PATRONS_TITLE"), L.title.w), L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);
    // The one lit: the Altar's faction's patron, or the one picked
    const int lit = patronPicking ? patronPick : patronOf(world, patronFaction);
    const float mx = (float)mouseX, my = (float)mouseY;
    int hover = -1;
    for (size_t i = 0; i < L.cells.size() && i < list.size(); ++i) {
        const Rect& c = L.cells[i];
        const bool on = (int)i == lit;
        if (c.contains(mx, my)) hover = (int)i;
        renderer.drawRect(Rect{ c.x - 3.0f * s, c.y - 3.0f * s, c.w + 6.0f * s, c.h + 6.0f * s }, on ? gold : RGBA{ 0.30f, 0.20f, 0.35f, 1.0f });
        renderer.drawPatronPicture((int)i, c);
        if (hover == (int)i && !on) renderer.drawRect(c, RGBA{ 1.0f, 1.0f, 1.0f, 0.12f });
    }
    if (patronPicking) {
        renderer.drawButton(L.random, lang("UILABEL_PATRON_RANDOM"), true, patronPick < 0 || L.random.contains(mx, my));
    }
    if (hover < 0) return;
    // The details of the one under the pointer
    const PatronInfo& p = list[(size_t)hover];
    std::vector<std::pair<std::string, RGBA>> lines;
    lines.push_back({ lang(p.nameKey), gold });
    lines.push_back({ p.mbti, dim });
    const float textW = 260.0f * s;
    for (const std::string& ln : renderer.wrapListText(lang(p.descKey), textW)) lines.push_back({ ln, white });
    if (!patronPicking) {
        if (const int holder = patronHolder(world, hover); holder >= 0) lines.push_back({ langf("UILABEL_PATRON_OF", { factionName(holder) }), dim });
    }
    const float pad = 8.0f * s;
    const float bw = textW + 2.0f * pad, bh = (float)lines.size() * textH + 2.0f * pad;
    const float bx = std::clamp(mx + 18.0f * s, 4.0f, std::max(4.0f, (float)winW - bw - 4.0f));
    const float by = std::clamp(my + 18.0f * s, 4.0f, std::max(4.0f, (float)winH - bh - 4.0f));
    renderer.drawRect(Rect{ bx - 1.5f * s, by - 1.5f * s, bw + 3.0f * s, bh + 3.0f * s }, RGBA{ 0.55f, 0.35f, 0.70f, 1.0f });
    renderer.drawRect(Rect{ bx, by, bw, bh }, RGBA{ 0.07f, 0.04f, 0.11f, 0.97f });
    float ty = by + pad;
    for (const auto& [text, tint] : lines) {
        renderer.drawListText(text, bx + pad, ty, tint);
        ty += textH;
    }
}

// ---- the banner

// The call, then the response. Yours is always on the right; of two others, the one answering is
void App::queuePatronChat(const PatronMemory& m, bool replay) {
    const bool callerRight = m.caller == FACTION_PLAYER;
    patronChat.queue.push_back({ m.callerPatron, callerRight, m.call, replay });
    patronChat.queue.push_back({ m.responderPatron, !callerRight, m.response, replay });
}

void App::replayPatronEvent(int index) {
    if (index < 0 || (size_t)index >= patronMemory.size() || !settings.patronDialogue) return;
    queuePatronChat(patronMemory[(size_t)index], true);
}

void App::patronSpeak(PatronEvent event, int caller, int responder, const std::string& base) {
    const int a = patronOf(world, caller), b = patronOf(world, responder);
    if (a < 0 || b < 0) return;
    PatronMemory m;
    m.event = event;
    m.caller = caller;
    m.responder = responder;
    m.callerPatron = a;
    m.responderPatron = b;
    // One of two versions of the pair (_CALL and _RESPONSE, or _CALL_2 and _RESPONSE_2), kept as said so that a replay says the same
    static Rng pick{ std::mt19937(0x7a7c0de5u) };
    const std::string v = pick.range(0, 1) == 0 ? "" : "_2";
    m.call = langf(base + "_CALL" + v, { factionName(responder) });
    m.response = langf(base + "_RESPONSE" + v, { factionName(caller) });
    patronMemory.push_back(m);
    const int index = (int)patronMemory.size() - 1;
    // The Patrons tab: what happened, then what each said
    const std::vector<PatronInfo>& list = patrons();
    auto nameOf = [&](int patron) { return (size_t)patron < list.size() ? lang(list[(size_t)patron].nameKey) : std::string(); };
    auto addLine = [&](const std::string& text, Tint tint) {
        LogLine ln;
        ln.text = text;
        ln.tint = tint;
        ln.replay = index;
        gameLog.patrons.push_back(ln);
    };
    const bool good = event == PatronEvent::Allied || event == PatronEvent::Trade || event == PatronEvent::Rescue;
    addLine(langf(base + "_TRIGGER", { factionName(caller), factionName(responder) }), event == PatronEvent::Idle ? Tint::Gray : good ? Tint::Green : Tint::Red);
    addLine(nameOf(a) + ": " + m.call, Tint::Gray);
    addLine(nameOf(b) + ": " + m.response, Tint::Gray);
    // The last 500 messages are kept (each event is its trigger and two messages: three lines)
    while (gameLog.patrons.size() > PATRON_KEEP_MESSAGES / 2 * 3) gameLog.patrons.erase(gameLog.patrons.begin(), gameLog.patrons.begin() + 3);
    if (settings.patronDialogue) queuePatronChat(m, false);
}

void App::updatePatronChat() {
    std::vector<PatronCue>& cues = world.resource<PatronTalk>().out;
    const Uint64 now = SDL_GetTicks();
    if (screen != Screen::Playing) {
        cues.clear();
        patronChat.lastTick = 0;
        return;
    }
    for (const PatronCue& c : cues) {
        const int a = patronOf(world, c.caller), b = patronOf(world, c.responder);
        if (a < 0 || b < 0) continue;
        const long long key = ((long long)c.event * 100 + std::min(c.caller, c.responder)) * 100 + std::max(c.caller, c.responder);
        const auto seen = patronChat.last.find(key);
        if (seen != patronChat.last.end() && gs.clockSeconds >= seen->second && gs.clockSeconds - seen->second < CHAT_SAME_SECONDS) continue;
        patronChat.last[key] = gs.clockSeconds;
        patronSpeak(c.event, c.caller, c.responder, patronEventKey(c.event));
        patronQuietSince = gs.clockSeconds;
        patronQuietStage = 0;
    }
    // Nothing between factions for 1, 2, 4, 8, then 16 hours (and every 16 after): two patrons, picked at random, pass the time
    {
        if (patronQuietSince < 0.0 || gs.clockSeconds < patronQuietSince) {
            patronQuietSince = gs.clockSeconds;
            patronQuietStage = 0;
        }
        static const double QUIET_HOURS[5] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
        const double need = patronQuietStage < 5 ? QUIET_HOURS[patronQuietStage] : 16.0 * (patronQuietStage - 3);
        if ((gs.clockSeconds - patronQuietSince) / 3600.0 >= need) {
            std::vector<int> talkers;
            for (const auto& [faction, patron] : world.resource<Patrons>().byFaction) talkers.push_back(faction);
            if (talkers.size() >= 2) {
                const int i = rng.range(0, (int)talkers.size() - 1);
                int j = rng.range(0, (int)talkers.size() - 2);
                if (j >= i) ++j;
                patronSpeak(PatronEvent::Idle, talkers[(size_t)i], talkers[(size_t)j], "PATRON_IDLE" + std::to_string(std::min(patronQuietStage, 4) + 1));
            }
            ++patronQuietStage;
        }
    }
    cues.clear();
    while (patronChat.queue.size() > CHAT_KEEP) patronChat.queue.pop_front();
    if (!settings.patronDialogue) {
        patronChat.queue.clear();
        patronChat.shown.clear();
        return;
    }
    // The banner: two lines at a time. A new line comes up from below and the older one moves up and is darkened; the one before that leaves.
    // The next line comes only when the newest has been up for 4 seconds, and the banner fades out 4 seconds after the last one.
    // The banner keeps its own clock, which stops while the game is paused (so a line stays up as long, and the fade waits)
    const bool frozen = session.paused || session.autoPaused;
    if (patronChat.lastTick && !frozen) patronChat.clock += std::min<Uint64>(now - patronChat.lastTick, 100);
    const float dt = patronChat.lastTick && !frozen ? std::min(0.1f, (float)(now - patronChat.lastTick) / 1000.0f) : 0.0f;
    patronChat.lastTick = now;
    const Uint64 chatNow = patronChat.clock;
    std::vector<ChatLine>& shown = patronChat.shown;
    ChatLine* newest = nullptr;
    for (ChatLine& ln : shown) {
        if (!ln.exiting) newest = &ln;
    }
    const bool waited = !newest || chatNow - newest->shownAt >= CHAT_STEP_MS;
    if (!patronChat.queue.empty() && waited) {
        ChatLine ln = patronChat.queue.front();
        patronChat.queue.pop_front();
        patronChat.history.push_back(ln);
        while (patronChat.history.size() > PATRON_KEEP_MESSAGES) patronChat.history.pop_front();
        // Only two stay: the oldest of them leaves upward
        int active = 0;
        for (const ChatLine& o : shown) active += o.exiting ? 0 : 1;
        for (ChatLine& o : shown) {
            if (active < CHAT_SHOWN) break;
            if (!o.exiting) {
                o.exiting = true;
                --active;
            }
        }
        ln.shownAt = chatNow;
        ln.alpha = 0.0f;
        ln.dim = 0.0f;
        shown.push_back(ln);
        patronChat.fadeOut = false;
        int k = 0;
        for (const ChatLine& o : shown) k += o.exiting ? 0 : 1;
        int i = 0;
        for (ChatLine& o : shown) {
            if (o.exiting) {
                o.slotTarget = -1.0f;
                continue;
            }
            o.slotTarget = (float)i++;
            if (&o == &shown.back()) o.slot = o.slotTarget + 0.8f;   // it comes up from below
        }
    } else if (patronChat.queue.empty() && newest && chatNow - newest->shownAt >= CHAT_STEP_MS) {
        patronChat.fadeOut = true;
    }
    newest = nullptr;   // (the vector may have grown)
    for (ChatLine& ln : shown) {
        if (!ln.exiting) newest = &ln;
    }
    for (size_t i = 0; i < shown.size(); ++i) {
        ChatLine& ln = shown[i];
        const bool latest = !ln.exiting && &ln == newest;
        const float ease = std::min(1.0f, dt * 10.0f);
        ln.slot += (ln.slotTarget - ln.slot) * ease;
        ln.alpha = std::clamp(ln.alpha + (ln.exiting ? -1.0f : 1.0f) * dt * 4.0f, 0.0f, 1.0f);
        ln.dim += ((latest ? 0.0f : 1.0f) - ln.dim) * std::min(1.0f, dt * 8.0f);
    }
    shown.erase(std::remove_if(shown.begin(), shown.end(), [](const ChatLine& ln) { return ln.exiting && ln.alpha <= 0.01f; }), shown.end());
    if (patronChat.fadeOut) {
        patronChat.fade = std::max(0.0f, patronChat.fade - dt / 0.7f);
        if (patronChat.fade <= 0.0f) {
            shown.clear();
            patronChat.fadeOut = false;
            patronChat.fade = 1.0f;
        }
    } else {
        patronChat.fade = std::min(1.0f, patronChat.fade + dt * 4.0f);
    }
}

void App::drawPatronChat() {
    if (screen != Screen::Playing || !settings.patronDialogue || patronChat.shown.empty()) return;
    const float s = uiScaleFor((float)winH), textH = renderer.listTextHeight("Ag");
    const float w = std::min((float)winW - 20.0f * s, 560.0f * s), pic = 64.0f * s, rowH = pic + 10.0f * s, gap = 6.0f * s;
    const float x0 = std::floor(((float)winW - w) * 0.5f);
    const float top = 10.0f * s;
    const std::vector<PatronInfo>& list = patrons();
    for (const ChatLine& ln : patronChat.shown) {
        const float a = ln.alpha * patronChat.fade;
        if (a < 0.01f) continue;
        const float k = 1.0f - 0.6f * ln.dim;   // the older line is darkened
        const float y = top + ln.slot * (rowH + gap);
        const RGBA gold{ 1.0f * k, 0.78f * k, 0.30f * k, a }, white{ k, k, k, a }, shade{ 0.0f, 0.0f, 0.0f, 0.85f * a };
        auto say = [&](const std::string& t, float x, float ty, RGBA tint) {
            renderer.drawListText(t, x + 1.0f, ty + 1.0f, shade);
            renderer.drawListText(t, x, ty, tint);
        };
        renderer.drawRect(Rect{ x0, y, w, rowH }, RGBA{ 0.04f, 0.02f, 0.08f, 0.70f * a });   // 70% opaque behind the line
        const float inset = 5.0f * s;
        const Rect picRect = ln.right ? Rect{ x0 + w - pic - inset, y + inset, pic, pic } : Rect{ x0 + inset, y + inset, pic, pic };
        renderer.drawRect(Rect{ picRect.x - 2.0f * s, picRect.y - 2.0f * s, picRect.w + 4.0f * s, picRect.h + 4.0f * s },
                          RGBA{ 0.55f * k, 0.35f * k, 0.70f * k, 0.9f * a });
        renderer.drawPatronPicture(ln.patron, picRect, RGBA{ k, k, k, a });
        const float textW = w - pic - 3.0f * inset - 8.0f * s;
        const float tx = ln.right ? x0 + inset + 4.0f * s : picRect.x + pic + inset + 4.0f * s;
        const std::string name = ln.patron >= 0 && (size_t)ln.patron < list.size() ? lang(list[(size_t)ln.patron].nameKey) : std::string();
        float ty = y + 7.0f * s;
        say(renderer.fitListText(ln.replay ? name + " " + lang("UILABEL_PATRON_REPLAY") : name, textW), tx, ty, gold);
        ty += textH;
        for (const std::string& t : renderer.wrapListText(ln.text, textW)) {
            if (ty + textH > y + rowH) break;
            say(t, tx, ty, white);
            ty += textH;
        }
    }
}
