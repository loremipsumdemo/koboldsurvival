#include "app/session.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "world/segments.hpp"
#include "world/worldfile.hpp"

namespace wm {


constexpr size_t kLogCap = 400;

std::string FormatDuration(double seconds) {
    if (seconds < 0) return "--";
    const long long t = static_cast<long long>(seconds + 0.5);
    char buf[64];
    if (t >= 3600) std::snprintf(buf, sizeof buf, "%lldh %02lldm", t / 3600, (t / 60) % 60);
    else if (t >= 60) std::snprintf(buf, sizeof buf, "%lldm %02llds", t / 60, t % 60);
    else std::snprintf(buf, sizeof buf, "%llds", t);
    return buf;
}

namespace {

// Appends a line with its elapsed time. The caller must hold lineMutex.
void PushLocked(Session& s, const std::string& text) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.genStart).count();
    char prefix[32];
    std::snprintf(prefix, sizeof prefix, "[%8s] ", FormatDuration(t).c_str());
    auto& log = s.genStatus.log;
    log.push_back(prefix + text);
    if (log.size() > kLogCap) log.erase(log.begin(), log.begin() + static_cast<long>(log.size() - kLogCap));
}

// Shortens a stage description to its name for the timing report: text before a colon.
std::string ShortLabel(const std::string& text) {
    const size_t colon = text.find(':');
    return colon == std::string::npos ? text : text.substr(0, colon);
}

// Adds the time since the open stage began to that stage. The caller must hold lineMutex.
void CloseStageLocked(Session& s, std::chrono::steady_clock::time_point now) {
    const int stage = s.genStatus.stage;
    if (stage > 0) {
        s.stageTimes[stage].seconds += std::chrono::duration<double>(now - s.stageStart).count();
    }
    s.stageStart = now;
}

}  // namespace

void BeginLog(Session& s, int stages, const std::string& title) {
    std::lock_guard<std::mutex> lock(s.lineMutex);
    s.genStatus = GenStatus{};
    s.genStatus.stages = stages;
    s.genStart = std::chrono::steady_clock::now();
    s.stageTimes.clear();
    s.stageStart = s.genStart;
    PushLocked(s, title);
}

void SetStage(Session& s, int stage, const std::string& name, const std::string& timingLabel) {
    std::lock_guard<std::mutex> lock(s.lineMutex);
    const auto now = std::chrono::steady_clock::now();
    CloseStageLocked(s, now);
    s.genStatus.stage = stage;
    s.genStatus.name = name;
    StageTiming& t = s.stageTimes[stage];
    if (t.label.empty()) t.label = ShortLabel(timingLabel.empty() ? name : timingLabel);
    PushLocked(s, "[Stage " + std::to_string(stage) + "/" + std::to_string(s.genStatus.stages) + "] " + name);
}

void FinishStages(Session& s) {
    std::lock_guard<std::mutex> lock(s.lineMutex);
    CloseStageLocked(s, std::chrono::steady_clock::now());
}

std::vector<std::string> TimingReport(Session& s) {
    std::vector<std::pair<std::string, double>> rows;
    double total = 0.0;
    {
        std::lock_guard<std::mutex> lock(s.lineMutex);
        for (const auto& [stage, t] : s.stageTimes) {
            (void)stage;
            if (t.seconds <= 0.0) continue;
            rows.emplace_back(t.label, t.seconds);
            total += t.seconds;
        }
    }
    std::vector<std::string> out;
    if (rows.empty() || total <= 0.0) return out;
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    out.push_back("Took " + FormatDuration(total) + " in total. Slowest stages:");
    for (size_t i = 0; i < rows.size() && i < 3; ++i) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "  %zu. %s  %.1fs  (%.0f%%)", i + 1, rows[i].first.c_str(), rows[i].second,
                      100.0 * rows[i].second / total);
        out.push_back(buf);
    }
    return out;
}

void AddLog(Session& s, const std::string& line) {
    std::lock_guard<std::mutex> lock(s.lineMutex);
    PushLocked(s, line);
}

GenStatus SnapshotStatus(Session& s) {
    std::lock_guard<std::mutex> lock(s.lineMutex);
    GenStatus st = s.genStatus;
    st.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.genStart).count();
    st.overall = s.progress.load() / 100.0;
    if (st.overall >= 1.0) st.eta = 0.0;
    else if (st.overall >= 0.02) st.eta = st.elapsed * (1.0 - st.overall) / st.overall;
    else st.eta = -1.0;
    return st;
}

namespace {

// Writes a large world as segment files, one segment at a time, on the worker thread.
void StartSegmentJob(Session& s, const GenSettings& settings) {
    chunkgen::Settings cs;
    cs.seed = settings.seed;
    cs.size = settings.size;
    cs.heightScale = settings.heightScale;
    cs.waterShare = settings.p.waterShare;
    cs.relief = settings.p.relief;
    cs.rangeCover = settings.p.rangeCover;
    cs.cover = settings.p.cover;

    s.generating = true;
    s.segmentJob = true;
    s.cancel.store(false);
    s.done.store(false);
    s.progress.store(0);
    s.segmentDir = s.worldsDir + "/segments_" + std::to_string(settings.seed) + "_" + std::to_string(settings.size);
    const std::string dir = s.segmentDir;
    BeginLog(s, 6, "Segment build: seed " + std::to_string(settings.seed) + ", " + std::to_string(settings.size) +
                       " tiles, writing to " + dir);

    s.worker = std::thread([&s, cs, dir]() {
        int lastStage = -1;
        std::string lastName;
        chunkgen::Tracker tr([&s, &lastStage, &lastName](const std::string& stage, int index, int, double overall,
                                                          double, double) {
            s.progress.store(static_cast<int>(overall * 100.0));
            if (index != lastStage || stage != lastName) {
                lastStage = index;
                lastName = stage;
                SetStage(s, index, stage, index == 5 ? "Writing segment files" : stage);
            }
        }, 6);
        std::string err;
        const bool ok = segments::Build(cs, dir, tr, s.cancel, err);
        std::string msg = ok ? "Segments written to " + dir : "Segment build stopped: " + err;
        if (ok && cs.size <= kViewableWorldLimit) {
            World loaded;
            if (segments::Assemble(dir, loaded, tr, s.cancel, err)) {
                s.pending = std::move(loaded);   // PollGenerate moves it into the viewer
                msg += "; loaded into the viewer";
            } else {
                msg = "Segments written, but loading them failed: " + err;
            }
        } else if (ok) {
            msg += "; too large to load into the viewer yet";
        }
        FinishStages(s);
        AddLog(s, msg);
        {
            std::lock_guard<std::mutex> lock(s.lineMutex);
            s.statusLine = msg;
        }
        s.done.store(true);
    });
}

}  // namespace

void StartGenerate(Session& s, const GenSettings& settings) {
    if (s.generating) return;
    if (settings.size > kWholeWorldLimit) {
        StartSegmentJob(s, settings);
        return;
    }

    s.lastSettings = settings;
    s.progress.store(0);
    s.done.store(false);
    s.generating = true;
    BeginLog(s, 6, "Whole-map generation: seed " + std::to_string(settings.seed) + ", " +
                       std::to_string(settings.size) + " tiles");

    // The worker only touches `pending`, `progress`, `done` and the log.
    s.worker = std::thread([&s, settings]() {
        const auto say = [&s](int stage, const std::string& text) {
            if (stage > 0) SetStage(s, stage, text);
            else AddLog(s, text);
        };
        s.gen.Run(settings, s.pending, s.progress, say);
        FinishStages(s);
        const std::string msg = "Finished in " + FormatDuration(SnapshotStatus(s).elapsed);
        AddLog(s, msg);
        {
            std::lock_guard<std::mutex> lock(s.lineMutex);
            s.statusLine = msg;
        }
        s.done.store(true);
    });
}

bool PollGenerate(Session& s) {
    if (!s.generating || !s.done.load()) return false;

    s.worker.join();
    if (s.segmentJob) {
        // A segment build writes files. When it was small enough, it also hands over a world to show.
        s.generating = false;
        s.segmentJob = false;
        if (s.pending.height.empty()) return false;
        s.world = std::move(s.pending);
        s.hasWorld = true;
        s.worldName.clear();
        s.terrainDirty = true;
        return true;
    }
    s.generating = false;
    s.world = std::move(s.pending);
    s.hasWorld = true;
    s.worldName.clear();
    s.terrainDirty = true;
    return true;
}

void JoinGenerate(Session& s) {
    if (s.worker.joinable()) s.worker.join();
    s.generating = false;
}

bool IsSafeName(const std::string& name) {
    if (name.empty() || name.size() > 60) return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == ' ')) return false;
    }
    return true;
}

bool SaveWorld(Session& s, const std::string& name, std::string& err) {
    if (!s.hasWorld) {
        err = "no world to save yet";
        return false;
    }
    if (!IsSafeName(name)) {
        err = "name may use letters, digits, space, - and _";
        return false;
    }
    if (!SaveWorldFile(s.world, s.worldsDir + "/" + name + ".wmw", err)) return false;
    s.worldName = name;
    return true;
}

bool LoadWorld(Session& s, const std::string& name, std::string& err) {
    if (!IsSafeName(name)) {
        err = "bad world name";
        return false;
    }
    World w;
    if (!LoadWorldFile(s.worldsDir + "/" + name + ".wmw", w, err)) return false;

    s.world = std::move(w);
    s.hasWorld = true;
    s.worldName = name;
    s.terrainDirty = true;
    return true;
}

}  // namespace wm
