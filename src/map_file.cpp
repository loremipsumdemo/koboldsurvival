#include "map_file.h"

MapData flatMap(int size) {
    MapData m;
    m.size = size;
    m.heights.assign((size_t)(size + 1) * (size + 1), 0.0f);
    m.walkable.assign((size_t)size * size, 1);
    m.biome.assign((size_t)size * size, (uint8_t)Biome::None);
    return m;
}
