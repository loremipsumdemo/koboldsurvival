#pragma once
#include <memory>
#include <string>
#include "chunk.h"
#include "world_format.h"

// Where a world's ground comes from. Terrain asks for chunks one at a time, as it needs them, and never needs the whole
// map at once. A source that works out its chunks instead of reading them only has to be another of these.
class ChunkSource {
public:
    virtual ~ChunkSource() = default;

    virtual int size() const = 0;
    virtual float floorY() const = 0;
    virtual const WorldSurface& surface() const = 0;
    // True: only the chunks around the camera and units are held for drawing. False: all of them always are.
    virtual bool streamed() const = 0;
    // The same chunk every time, whatever was asked for before. False if there is none. Called from other threads too, so
    // it must not change anything.
    virtual bool fill(int cx, int cz, ChunkData& out) const = 0;
};

// Flat ground with no water, all of it walkable
std::shared_ptr<const ChunkSource> flatChunkSource(int size);
// A world folder (world_format.h). Null, with the reason in err, if it can't be read.
std::shared_ptr<const ChunkSource> worldFolderSource(const std::string& folder, std::string& err);
