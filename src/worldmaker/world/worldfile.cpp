#include "world/worldfile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace wm {


namespace fs = std::filesystem;

namespace {

constexpr char kMagic[4] = {'W', 'M', 'W', '1'};
constexpr uint32_t kVersion = 1;

template <typename T>
void Put(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool Get(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(f);
}

}  // namespace

bool SaveWorldFile(const World& w, const std::string& path, std::string& err) {
    const std::size_t cells = static_cast<std::size_t>(w.size) * static_cast<std::size_t>(w.size);
    if (w.size <= 0 || w.height.size() != cells || w.biome.size() != cells || w.veg.size() != cells) {
        err = "world is empty or inconsistent";
        return false;
    }

    const fs::path target = fs::u8path(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

    const fs::path tmp = fs::u8path(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = "could not open for writing: " + tmp.string();
            return false;
        }
        f.write(kMagic, 4);
        Put(f, kVersion);
        Put(f, static_cast<int32_t>(w.size));
        Put(f, w.seed);
        Put(f, w.seaLevel);
        f.write(reinterpret_cast<const char*>(w.height.data()),
                static_cast<std::streamsize>(cells * sizeof(float)));
        f.write(reinterpret_cast<const char*>(w.biome.data()),
                static_cast<std::streamsize>(cells * sizeof(uint8_t)));
        f.write(reinterpret_cast<const char*>(w.veg.data()),
                static_cast<std::streamsize>(cells * sizeof(float)));
        f.flush();
        if (!f) {
            err = "write failed: " + tmp.string();
            fs::remove(tmp, ec);
            return false;
        }
    }

    // Same-folder rename replaces the old file in one step.
    fs::rename(tmp, target, ec);
    if (ec) {
        err = "could not replace " + path + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool LoadWorldFile(const std::string& path, World& out, std::string& err) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) {
        err = "could not open " + path;
        return false;
    }

    char magic[4] = {};
    f.read(magic, 4);
    if (!f || std::memcmp(magic, kMagic, 4) != 0) {
        err = "not a worldmaker world file";
        return false;
    }

    uint32_t version = 0;
    int32_t size = 0;
    uint32_t seed = 0;
    float sea = 0.0f;
    if (!Get(f, version) || !Get(f, size) || !Get(f, seed) || !Get(f, sea)) {
        err = "file header is truncated";
        return false;
    }
    if (version != kVersion) {
        err = "unsupported world version " + std::to_string(version);
        return false;
    }
    if (size <= 0 || size > 4096) {
        err = "bad world size " + std::to_string(size);
        return false;
    }

    const std::size_t cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    World w;
    w.size = size;
    w.seed = seed;
    w.seaLevel = sea;
    w.height.resize(cells);
    w.biome.resize(cells);
    w.veg.resize(cells);

    f.read(reinterpret_cast<char*>(w.height.data()), static_cast<std::streamsize>(cells * sizeof(float)));
    f.read(reinterpret_cast<char*>(w.biome.data()), static_cast<std::streamsize>(cells * sizeof(uint8_t)));
    f.read(reinterpret_cast<char*>(w.veg.data()), static_cast<std::streamsize>(cells * sizeof(float)));
    if (!f) {
        err = "file is truncated";
        return false;
    }

    out = std::move(w);
    return true;
}

std::vector<std::string> ListWorldNames(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return names;

    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() == ".wmw") names.push_back(e.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace wm
