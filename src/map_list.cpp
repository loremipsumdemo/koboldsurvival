#include "level.h"
#include <string>
#include <vector>
#include "pak.h"
#include "world_format.h"
#include "world_gen.h"

bool mainMapPacked() {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    return pakFind(std::string("data/map/") + MAIN_MAP_STEM + "/world.txt", bytes, size);
}

std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    if (mainMapPacked()) {
        out.push_back(MapChoice{ "packed", MAIN_MAP_STEM, MAIN_MAP_STEM,
                                 std::string(PAK_WORLD_PREFIX) + "data/map/" + MAIN_MAP_STEM });
    }
    for (const std::string& name : listMapFiles()) out.push_back(MapChoice{ MAP_SOURCE_FILE, name, name, mapFilePath(name) });
    return out;
}
