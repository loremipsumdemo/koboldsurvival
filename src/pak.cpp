#include "pak.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace {

// File layout, which tools/pack_data.cpp writes:
//   8 bytes   magic "RTSPAK01"
//   4 bytes   number of files
//   per file: 4 bytes name length, the name, 8 bytes offset, 8 bytes size
//   then the file bytes themselves
// Offsets count from the start of the pak. Numbers are little-endian.
const char MAGIC[8] = { 'R', 'T', 'S', 'P', 'A', 'K', '0', '1' };

struct Entry {
    uint64_t offset = 0;
    uint64_t size = 0;
};

std::vector<unsigned char> g_bytes;                  // the whole pak, kept for the program's life
std::unordered_map<std::string, Entry> g_index;      // file name -> where its bytes are

// Reads a little-endian integer at pos and moves pos past it. Returns false past the end.
template <typename T>
bool readInt(const std::vector<unsigned char>& b, size_t& pos, T& out) {
    if (pos + sizeof(T) > b.size()) return false;
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<T>(b[pos + i]) << (8 * i);
    }
    out = v;
    pos += sizeof(T);
    return true;
}

} // namespace

bool pakOpen(const std::string& pakPath) {
    g_index.clear();
    g_bytes.clear();

    std::ifstream in(pakPath, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize size = in.tellg();
    if (size <= 0) return false;
    in.seekg(0, std::ios::beg);
    g_bytes.resize((size_t)size);
    if (!in.read(reinterpret_cast<char*>(g_bytes.data()), size)) return false;

    if (g_bytes.size() < sizeof(MAGIC) || std::memcmp(g_bytes.data(), MAGIC, sizeof(MAGIC)) != 0) {
        return false;
    }

    size_t pos = sizeof(MAGIC);
    uint32_t count = 0;
    if (!readInt(g_bytes, pos, count)) return false;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nameLen = 0;
        if (!readInt(g_bytes, pos, nameLen)) return false;
        if (pos + nameLen > g_bytes.size()) return false;
        std::string name(reinterpret_cast<const char*>(g_bytes.data() + pos), nameLen);
        pos += nameLen;

        Entry e;
        if (!readInt(g_bytes, pos, e.offset)) return false;
        if (!readInt(g_bytes, pos, e.size)) return false;
        if (e.offset > g_bytes.size() || e.size > g_bytes.size() - e.offset) return false;
        g_index[name] = e;
    }
    return true;
}

bool pakFind(const std::string& name, const unsigned char*& bytes, size_t& size) {
    auto it = g_index.find(name);
    if (it == g_index.end()) return false;
    bytes = g_bytes.data() + it->second.offset;
    size = (size_t)it->second.size;
    return true;
}

SDL_Surface* pakImage(const std::string& name) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(name, bytes, size)) {
        SDL_Log("Not in data.pak: %s", name.c_str());
        return nullptr;
    }
    // closeio = true: SDL frees its stream object. The bytes belong to the pak and are not freed.
    return IMG_Load_IO(SDL_IOFromConstMem(bytes, size), true);
}

TTF_Font* pakFont(const std::string& name, float size) {
    const unsigned char* bytes = nullptr;
    size_t byteCount = 0;
    if (!pakFind(name, bytes, byteCount)) {
        SDL_Log("Not in data.pak: %s", name.c_str());
        return nullptr;
    }
    return TTF_OpenFontIO(SDL_IOFromConstMem(bytes, byteCount), true, size);
}
