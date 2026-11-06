#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "chunk.h"

struct OverviewPicture {
    int size = 0;
    int step = 1;
    int px = 0;
    std::vector<uint8_t> rgba;
    float extent() const { return static_cast<float>(px) * static_cast<float>(step); }
};

using ChunkSource = std::function<bool(int cx, int cz, ChunkData& out)>;

void BuildOverview(int size, const ChunkSource& source, OverviewPicture& out, const std::atomic<bool>& cancel);

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
    bool pending_ = false;
    OverviewPicture pic_;
};
