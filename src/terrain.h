#pragma once
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "chunk.h"
#include "chunk_source.h"
#include "geometry.h"
#include "grid.h"
#include "world_format.h"

constexpr float AGENT_CLEARANCE = 0.3f;

// How many chunks of ground Terrain keeps in memory for the simulation. Asking for another drops the one unused longest,
// and it comes back from the source when next needed, so the map can be far larger than this.
constexpr size_t MAX_GROUND_CHUNKS = 1024;

// Terrain holds the ground of the level being played: heights, walkability and ground types, in chunks of CHUNK_TILES
// tiles, which it asks its ChunkSource for. It starts as a blank flat map.
// Two sets of chunks are held. For drawing, the ones around the camera and the units (stream()). For the simulation, the
// ground of whichever chunks were last asked about, so units, paths and sight work the same everywhere, whatever is drawn.
inline constexpr float DECK_HEIGHT = 0.2f;   // a finished foundation's top above the ground (its model is 0.2 tall)

class Terrain {
public:
    explicit Terrain(int size = DEFAULT_MAP_SIZE);
    explicit Terrain(std::shared_ptr<const ChunkSource> source);

    // On failure, the terrain is left as it was
    bool openWorld(const std::string& folder, std::string& err);
    void open(std::shared_ptr<const ChunkSource> source);

    int size() const { return size_; }
    const std::shared_ptr<const ChunkSource>& source() const { return source_; }
    int chunksPerSide() const { return cps_; }
    // The chunks held for drawing, by id (chunk row times chunksPerSide, plus column), in no order: all of them for a map that is not streamed,
    // else the ones round the camera and the units. What is done for the ground each frame goes by this list and never over every chunk of
    // the map, so that a frame costs what is held, whatever the size of the map.
    const std::vector<int>& drawnChunks() const { return drawn_; }
    size_t groundChunksHeld() const { return groundHeld_.size(); }
    const ChunkData* chunkAt(int cx, int cz) const;

    int stream(const std::vector<Vec2>& focuses, int radius, int maxLoads);
    // The whole world's ground, coarse (a streamed world only), for drawing past the chunks held. Null until it is made, which
    // starts at the first ask, on another thread; and null for a source that can't make one.
    struct FarField {
        int res = 0;
        float spacing = 0.0f;   // tiles between corners
        std::vector<float> heights;
        std::vector<uint8_t> rgb;
    };
    const FarField* farField() const;
    // How many corners across the whole world's far ground has: about a sample every 4 tiles up to 2048 tiles across, and past that as fine as
    // the limit lets it be (the mesh is res squared vertices). A bigger map has its far ground as a window that follows the camera instead
    // once that is finer (Renderer).
    static int farFieldRes(int mapSize) { return mapSize <= 2048 ? 513 : 1025; }
    bool streamed() const { return streamed_; }

    float heightAt(float x, float z) const;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < size_ && y < size_; }
    bool walkable(int x, int y) const;
    bool tileGround(int x, int y, bool& walkable, float& height) const;
    const WorldSurface& surface() const { return surface_; }
    uint32_t surfaceVersion() const { return surfaceVersion_; }
    float heightScale() const { return heightScale_; }
    float floorY() const { return floorY_; }

    bool isFree(float x, float z) const;
    // Tiles closed by buildings (walls), on top of the ground's own walkability. Opening a map clears them.
    void setBlocked(int x, int y, bool blocked);
    // Clears every closed tile, for them to be set again; settleBlocked afterwards (the navigation regions then only redo the clusters whose
    // tiles ended up different)
    void clearBlocked();
    void settleBlocked() { navSettleBlocked(); }
    // A building's tile that blocks sight up to this height above the ground (0: none); sightClear looks at them as it looks at the ground
    void setSightBlock(int x, int y, float top);
    bool blockedByBuilding(int x, int y) const;
    // Tiles under a finished foundation's deck: a unit there stands DECK_HEIGHT above the ground at the tile's middle
    void setDeck(int x, int y, bool on);
    void clearDecks() { decks_.clear(); }
    bool onDeck(int x, int y) const;
    // Where something standing at x, z is: the ground, or the top of a foundation's deck
    float standHeight(float x, float z) const;
    // Ground reshaped in play (foundations levelling it): corner heights by cornerKey, laid over what the map's source
    // gives. Opening a map clears them; a save's are put back with setHeightEdits.
    static long long cornerKey(int x, int z) { return (long long)z * 1000003LL + (long long)x; }
    void setCornerHeight(int x, int z, float h);
    void setHeightEdits(const std::unordered_map<long long, float>& edits);
    // The chunk made again from its source (a source that changes, as the Map Editor's does): the ground held and the chunk drawn both
    void reloadChunk(int cx, int cz);
    const std::unordered_map<long long, float>& heightEdits() const { return heightEdits_; }
    // The piece of open ground a tile is in, numbered from 1; 0 for closed ground. On a map too big to work them out, all
    // open ground is piece 1.
    int areaAt(int x, int y) const { return walkable(x, y) ? areaOf(x, y) : 0; }
    // How many tiles each piece has, by its number. Empty on a map too big to work them out.
    std::vector<int> areaSizes() const;
    // Navigation, kept up to date a cluster at a time as tiles close and open (nav_regions.cpp): whether two tiles are open ground that joins up
    // now, the buildings' closed tiles counted (the areas above are of the bare ground, worked out once). prepareNav makes the regions the first
    // time (a pass over the map) and brings them up to date; everything that asks does so itself. A map too big for it answers as the areas do.
    void prepareNav() const;
    bool connected(int ax, int az, int bx, int bz) const;
    // The piece of ground a tile is in, as connected sees it (numbered from 1), or 0 for a closed tile
    int navPieceAt(int x, int z) const;
    Vec2 snap(Vec2 p) const;
    // The last resort of a placement: the open tile nearest `from` on a piece of ground that is big (within a tenth of the biggest piece).
    // Ties go to the tile met first in scanning order, so the same map always gives the same answer. False on a map too big for the pieces
    // to be worked out, or with no open ground.
    bool nearestBigLand(Vec2 from, Vec2& out) const;

    std::vector<Vec2> findPath(Vec2 start, Vec2 goal) const;
    bool lineClear(Vec2 a, Vec2 b) const;
    bool sightClear(float ax, float ay, float az, float bx, float by, float bz) const;

private:
    struct GroundChunk {
        std::vector<float> heights;
        std::vector<uint8_t> walk;
        uint64_t lastUsed = 0;
    };
    const GroundChunk& groundOf(int cx, int cz) const;
    void findAreas();
    void applyEdits(int cx, int cz, std::vector<float>& heights) const;
    void refreshCorner(int x, int z);
    int areaOf(int x, int y) const;
    struct Nav;
    mutable std::shared_ptr<Nav> nav_;
    void navTileChanged(int x, int z);
    // Hierarchical path finding (nav_regions.cpp): marks the regions a walk between the two tiles goes through, and the regions beside
    // them; false if there is no way, or no regions (a map too big for them). navAllowed then says whether a tile is among those marked.
    bool navCorridor(int sx, int sz, int gx, int gz) const;
    bool navAllowed(int x, int z) const;
    void navStashBlocked();
    void navSettleBlocked();

    int size_ = 0;
    int cps_ = 0;
    std::vector<std::unique_ptr<ChunkData>> chunks_;
    std::vector<int> drawn_;
    mutable std::vector<std::unique_ptr<GroundChunk>> ground_;
    mutable std::vector<int> groundHeld_;
    mutable uint64_t groundClock_ = 0;
    std::vector<int> groundArea_;
    bool streamed_ = false;
    std::shared_ptr<const ChunkSource> source_;
    uint32_t nextVersion_ = 1;
    WorldSurface surface_;
    uint32_t surfaceVersion_ = 0;
    float heightScale_ = 1.0f;
    float floorY_ = -1.0f;
    std::unordered_set<long long> blocked_;
    // The closed tiles again, as a bit a tile in each chunk (a chunk's row is one 64-bit word): what walkable asks, many times a step
    // for every unit and every tile a path search opens, without hashing. Kept in step with blocked_ by setBlocked and clearBlocked.
    struct BlockedBits {
        uint64_t rows[CHUNK_TILES] = {};
    };
    static_assert(CHUNK_TILES == 64, "a chunk's row of closed tiles is one 64-bit word");
    std::vector<std::unique_ptr<BlockedBits>> blockedBits_;
    std::vector<int> blockedChunks_;   // the ids of the chunks that have any of blockedBits_, so clearing them is a look at those and not at every chunk of the map
    std::unordered_map<long long, float> sightBlocks_;
    std::unordered_set<long long> decks_;
    std::unordered_map<long long, float> heightEdits_;
    mutable std::future<std::shared_ptr<FarField>> farJob_;
    mutable std::shared_ptr<FarField> far_;
    mutable bool farDone_ = false;
};
