#pragma once

// Builds a world as segment files, one 1024 x 1024 segment at a time, so memory stays
// bounded by a single segment however large the world is.

#include <atomic>
#include <string>
#include <vector>

#include "world/chunk_gen.hpp"

namespace wm {


namespace segments {

constexpr int kSegment = 1024;  // tiles per side of one segment file

// Path of the file for segment (sx, sz) in dir.
std::string SegmentPath(const std::string& dir, int sx, int sz);

// Writes world.txt and one file per segment into dir. The caller's tracker should have
// six stages: four for preparation, one for the segments, one for loading. Stops early when cancel
// is set. Returns false and sets err on failure.
bool Build(const chunkgen::Settings& s, const std::string& dir, chunkgen::Tracker& tr,
           const std::atomic<bool>& cancel, std::string& err);

// Joins the segment files in dir into one world for the viewer. Reports stage 5 through tr.
bool Assemble(const std::string& dir, World& out, chunkgen::Tracker& tr, const std::atomic<bool>& cancel,
              std::string& err);

// Reads one segment file back as its chunks, full detail.
bool ReadSegment(const std::string& path, std::vector<chunkgen::Chunk>& out);

}  // namespace segments

}  // namespace wm
