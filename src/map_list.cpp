#include "level.h"
#include <string>
#include <vector>
#include "world_format.h"
#include "world_gen.h"

std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    for (const std::string& name : listMapFiles()) out.push_back(MapChoice{ MAP_SOURCE_FILE, name, name, mapFilePath(name) });
    return out;
}
