#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "world/world.hpp"

namespace wm {


// What the running generation is doing. Shown in the menu under the progress bar.
struct GenStatus {
    int stage = 0;                 // current stage, 1-based; 0 before the first stage
    int stages = 0;                // stages in this job
    std::string name;              // what the current stage is doing
    double elapsed = 0.0;          // seconds since the job started
    double eta = -1.0;             // seconds left, -1 while still estimating
    double overall = 0.0;          // 0 to 1
    std::vector<std::string> log;  // timestamped lines, oldest first, capped
};

// Total time spent in one stage of a generation, and what to call it in the report.
struct StageTiming {
    std::string label;
    double seconds = 0.0;
};

// The world the walker is in, plus the background generation that makes new ones.
// One Session lives for the whole program. Only the main thread touches it, except
// for the worker thread, which writes `pending` and sets `done` when it finishes.
struct Session {
    World world;                     // the loaded or generated world
    bool hasWorld = false;
    bool terrainDirty = false;       // true when the renderer must rebuild terrain

    std::string worldName;           // name it was saved or loaded under, empty if unsaved

    // Generation
    GenSettings lastSettings;        // settings used by the most recent generate
    WorldGen gen;                    // keeps finished stages between generates; worker only
    bool generating = false;
    std::thread worker;
    World pending;                   // written by the worker thread
    std::atomic<int>  progress{0};
    std::atomic<bool> done{false};

    // Very large worlds are written as segment files instead of being held in memory.
    bool segmentJob = false;         // the running generation writes segment files
    std::string segmentDir;          // where the segment files go
    std::atomic<bool> cancel{false}; // set to stop a segment build
    std::mutex lineMutex;            // guards statusLine, genStatus and genStart
    std::string statusLine;          // summary of the last finished job
    GenStatus genStatus;             // stage, log and timing of the running job
    std::chrono::steady_clock::time_point genStart;
    std::map<int, StageTiming> stageTimes;   // time spent in each stage index
    std::chrono::steady_clock::time_point stageStart;  // when the open stage began

    // Writes a world as a game map, under the name typed in the menu. Set by the host, which
    // knows the game's map format. Returns false with err when the export fails.
    std::function<bool(const World& w, const std::string& name, std::string& err)> exportMap;

    std::string worldsDir;           // <exe>/data/worlds
    std::string themesDir;           // <exe>/data/themes
};

// Worlds larger than this are written as segment files.
constexpr int kWholeWorldLimit = 4096;

// Segment builds up to this size are joined into one world for the viewer after they are written.
constexpr int kViewableWorldLimit = 8192;

// Formats seconds as 42s, 3m 07s or 1h 05m. Negative values give "--".
std::string FormatDuration(double seconds);

// Starts the log for a new job with the number of stages it will report.
void BeginLog(Session& s, int stages, const std::string& title);
// Moves the job to a new stage and logs it. Safe to call from the worker thread.
// timingLabel names the stage in the timing report; empty uses the name.
void SetStage(Session& s, int stage, const std::string& name, const std::string& timingLabel = "");
// Adds a detail line to the log. Safe to call from the worker thread.
void AddLog(Session& s, const std::string& line);
// Copies the current status, with elapsed time and ETA worked out now.
GenStatus SnapshotStatus(Session& s);
// Closes the open stage. Call once when the job's work is done.
void FinishStages(Session& s);
// Lines reporting the total time and the slowest stages of the last job. Empty before any job.
std::vector<std::string> TimingReport(Session& s);

// Starts generating in the background. Does nothing if already generating.
void StartGenerate(Session& s, const GenSettings& settings);

// Call once per frame. When a generation finishes, moves the new world in and
// sets terrainDirty. Returns true when that happened.
bool PollGenerate(Session& s);

// Waits for any running generation to finish. Call before exiting.
void JoinGenerate(Session& s);

// Writes the loaded world to <worldsDir>/<name>.wmw. Name must be a plain file name.
bool SaveWorld(Session& s, const std::string& name, std::string& err);

// Reads <worldsDir>/<name>.wmw and makes it the current world. Sets terrainDirty.
bool LoadWorld(Session& s, const std::string& name, std::string& err);

// True when name is safe to use as a file name (letters, digits, - _ and space).
bool IsSafeName(const std::string& name);

}  // namespace wm
