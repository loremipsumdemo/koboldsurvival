#pragma once

// The whole level as one small picture, for the minimap and the world map. A picture pixel covers
// `step` tiles. Its left edge is the largest x and its top edge is the largest z, so it reads north up
// with +X on the left, as the worldmaker overview and preview do. The picture covers px * step tiles,
// which is at least the level's size; the part past the level is never drawn.

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "chunk.h"

struct OverviewPicture {
    int size = 0;                 // tiles per side of the level
    int step = 1;                 // tiles per picture pixel
    int px = 0;                   // picture pixels per side
    std::vector<uint8_t> rgba;    // px * px * 4, row by row from the top
    // Tiles covered by the picture per side, for turning a tile coordinate into a texture coordinate
    float extent() const { return static_cast<float>(px) * static_cast<float>(step); }
};

// Gives chunk (cx, cz) of a level into out. False if the chunk cannot be read; it is then taken as sea.
using ChunkSource = std::function<bool(int cx, int cz, ChunkData& out)>;

// Makes the picture of a level size tiles across from its chunks. Stops early, leaving the picture
// partly filled, when cancel is set.
void BuildOverview(int size, const ChunkSource& source, OverviewPicture& out, const std::atomic<bool>& cancel);

// Builds a picture on a worker thread (background) or right away. Start stops any build in progress first.
// take hands the finished picture over once, on the thread that owns the renderer.
class OverviewBuilder {
public:
    OverviewBuilder() = default;
    ~OverviewBuilder() { stop(); }
    OverviewBuilder(const OverviewBuilder&) = delete;
    OverviewBuilder& operator=(const OverviewBuilder&) = delete;

    void start(int size, ChunkSource source, bool background);
    void stop();
    bool take(OverviewPicture& out);

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    bool pending_ = false;        // started and not yet taken
    OverviewPicture pic_;
};
