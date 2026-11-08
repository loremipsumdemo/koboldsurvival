#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "level.h"
#include "level_file.h"
#include "systems.h"
#include "ui_types.h"

class ChunkSource;

// Works out, away from the frame loop, where a random set-up on a map puts your start and the factions' camps
class LayoutPlanner {
public:
    ~LayoutPlanner() { stop(); }
    void start(std::shared_ptr<const ChunkSource> source);
    // Stops the work under way, if any, and waits for it
    void stop();
    // The plan, once, when it is done
    bool take(LayoutPlan& out);

private:
    std::thread worker_;
    std::atomic<bool> cancel_{ false };
    std::mutex lock_;
    bool done_ = false;
    LayoutPlan plan_;
};

// New Game's first page: which map, and which level on it (or a random set-up), with a picture of the map
struct MapPickUi {
    std::vector<MapChoice> maps;
    std::vector<std::string> levels;   // the level files for the picked map, after "" for a random set-up
    int map = 0, level = 0;
    int mapScroll = 0, levelScroll = 0;
    bool previewReady = false;
    int previewSize = 0;
    float previewExtent = 1.0f;        // how many tiles across the picture shows
    LevelData levelData;               // the picked level, for its marks on the picture
    bool levelRead = false;
    bool planReady = false;            // where a random set-up puts things on the picked map
    LayoutPlan plan;
};

struct MapPickLayout {
    Rect box, title, back, next;
    Rect mapsHead, levelsHead, mapsArea, levelsArea;
    std::vector<Rect> mapRows, levelRows;
    int mapFirst = 0, levelFirst = 0;
    Rect previewHead, preview, note;
};
MapPickLayout layoutMapPick(int W, int H, int maps, int levels, int mapScroll, int levelScroll);
