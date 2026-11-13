#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Both come with tinygltf, which the game already uses to read its models
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace {
const char MAGIC[8] = { 'R', 'T', 'S', 'P', 'A', 'K', '0', '1' };

struct PackedFile {
    std::string name;
    fs::path source;
    std::vector<unsigned char> bytes;   // as packed: see packedBytes
    uint64_t offset = 0;
};

template <typename T>
void writeInt(std::ostream& out, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.put(static_cast<char>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF));
    }
}

// Transparent and nearly transparent pixels still carry a color, often junk (an alpha of 2 with a bright
// red, say). Texture filtering and mipmaps blend it into the visible edge, which shows as stray specks
// and fringes. So each such pixel takes the average color of its nearest visible neighbors, spreading
// outward; alpha stays as it was. Done here, once, so the game does not do it for every picture at every start.
void bleedIntoTransparent(unsigned char* base, int w, int h) {
    if (w <= 0 || h <= 0) return;
    constexpr unsigned char VISIBLE_ALPHA = 26;   // the glyph shader's 0.1 cutoff
    auto px = [&](int x, int y) { return base + ((size_t)y * (size_t)w + (size_t)x) * 4; };

    std::vector<unsigned char> known((size_t)w * (size_t)h, 0);
    std::vector<int> frontier, next;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (px(x, y)[3] >= VISIBLE_ALPHA) known[(size_t)y * (size_t)w + (size_t)x] = 1;
        }
    }
    // The unknown pixels next to the given known ones, each once. `queued` marks pixels already in a ring.
    std::vector<unsigned char> queued((size_t)w * (size_t)h, 0);
    auto ringAround = [&](const std::vector<int>& from, std::vector<int>& out) {
        out.clear();
        for (int i : from) {
            const int x = i % w, y = i / w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t j = (size_t)ny * (size_t)w + (size_t)nx;
                    if (known[j] || queued[j]) continue;
                    queued[j] = 1;
                    out.push_back((int)j);
                }
            }
        }
    };
    // The visible pixels that border a see-through one: only these can start the first ring
    std::vector<int> edge;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!known[(size_t)y * (size_t)w + (size_t)x]) continue;
            bool border = false;
            for (int dy = -1; dy <= 1 && !border; ++dy) {
                for (int dx = -1; dx <= 1 && !border; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    border = !known[(size_t)ny * (size_t)w + (size_t)nx];
                }
            }
            if (border) edge.push_back(y * w + x);
        }
    }
    // A few rings outward covers what filtering and the first mipmaps can reach
    constexpr int RINGS = 8;
    ringAround(edge, frontier);
    for (int ring = 0; ring < RINGS && !frontier.empty(); ++ring) {
        for (int i : frontier) {
            const int x = i % w, y = i / w;
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    if (!known[(size_t)ny * (size_t)w + (size_t)nx]) continue;
                    const unsigned char* q = px(nx, ny);
                    r += q[0]; g += q[1]; b += q[2]; ++n;
                }
            }
            unsigned char* p = px(x, y);
            p[0] = (unsigned char)(r / n); p[1] = (unsigned char)(g / n); p[2] = (unsigned char)(b / n);
        }
        for (int i : frontier) known[(size_t)i] = 1;
        if (ring + 1 < RINGS) {
            ringAround(frontier, next);
            frontier.swap(next);
        }
    }
}

bool isPng(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".png";
}

std::vector<unsigned char> readAll(const fs::path& p, bool& ok) {
    std::ifstream in(p, std::ios::binary);
    ok = (bool)in;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The file as it goes into the pak: a picture with its see-through pixels filled in, anything else as it is. A picture
// with nothing see-through, or one that cannot be read as a picture, also goes in as it is.
std::vector<unsigned char> packedBytes(const fs::path& source, bool& ok) {
    std::vector<unsigned char> bytes = readAll(source, ok);
    if (!ok || !isPng(source)) return bytes;
    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, 4);
    if (!rgba) return bytes;
    bool seeThrough = false;
    for (size_t i = 0, n = (size_t)w * (size_t)h; i < n && !seeThrough; ++i) seeThrough = rgba[i * 4 + 3] < 255;
    if (seeThrough) {
        bleedIntoTransparent(rgba, w, h);
        std::vector<unsigned char> filled;
        const auto append = [](void* to, void* data, int size) {
            auto* out = static_cast<std::vector<unsigned char>*>(to);
            out->insert(out->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
        };
        if (stbi_write_png_to_func(append, &filled, w, h, 4, rgba, w * 4) && !filled.empty()) bytes.swap(filled);
    }
    stbi_image_free(rgba);
    return bytes;
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: pack_data <folder> <output.pak>\n";
        return 1;
    }

    const fs::path root(argv[1]);
    if (!fs::is_directory(root)) {
        std::cerr << "pack_data: not a folder: " << root.string() << "\n";
        return 1;
    }
    const std::string prefix = root.filename().string();

    std::vector<PackedFile> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        PackedFile f;
        f.name = prefix + "/" + fs::relative(entry.path(), root).generic_string();
        f.source = entry.path();
        bool ok = false;
        f.bytes = packedBytes(f.source, ok);
        if (!ok) {
            std::cerr << "pack_data: could not read " << f.source.string() << "\n";
            return 1;
        }
        files.push_back(std::move(f));
    }
    std::sort(files.begin(), files.end(),
              [](const PackedFile& a, const PackedFile& b) { return a.name < b.name; });

    uint64_t offset = sizeof(MAGIC) + 4;
    for (const PackedFile& f : files) offset += 4 + f.name.size() + 8 + 8;
    for (PackedFile& f : files) {
        f.offset = offset;
        offset += f.bytes.size();
    }

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::cerr << "pack_data: could not write " << argv[2] << "\n";
        return 1;
    }

    out.write(MAGIC, sizeof(MAGIC));
    writeInt(out, (uint32_t)files.size());
    for (const PackedFile& f : files) {
        writeInt(out, (uint32_t)f.name.size());
        out.write(f.name.data(), (std::streamsize)f.name.size());
        writeInt(out, f.offset);
        writeInt(out, (uint64_t)f.bytes.size());
    }
    for (const PackedFile& f : files) {
        out.write(reinterpret_cast<const char*>(f.bytes.data()), (std::streamsize)f.bytes.size());
    }

    if (!out) {
        std::cerr << "pack_data: write failed for " << argv[2] << "\n";
        return 1;
    }
    std::cout << "pack_data: wrote " << files.size() << " files to " << argv[2] << "\n";
    return 0;
}
