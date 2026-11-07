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

using OverviewChunks = std::function<bool(int cx, int cz, ChunkData& out)>;
// Makes the whole picture at once, as ChunkSource::picture does. Tried first; the chunks are only read if it says no.
using OverviewSampler = std::function<bool(int px, std::vector<uint8_t>& rgba, const std::atomic<bool>& cancel)>;

void BuildOverview(int size, const OverviewChunks& source, const OverviewSampler& sampler, OverviewPicture& out, const std::atomic<bool>& cancel);

class OverviewBuilder {
public:
    OverviewBuilder() = default;
    ~OverviewBuilder() { stop(); }
    OverviewBuilder(const OverviewBuilder&) = delete;
    OverviewBuilder& operator=(const OverviewBuilder&) = delete;

    void start(int size, OverviewChunks source, OverviewSampler sampler, bool background);
    void stop();
    bool take(OverviewPicture& out);

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    bool pending_ = false;
    OverviewPicture pic_;
};
