// Packs a folder into one pak file, so the game ships a single data.pak instead of loose files.
// Usage: pack_data <folder> <output.pak>
// Each file is stored under the folder's name, such as "data/unit/unit_1.png" for data/unit/unit_1.png.
// The layout must match src/pak.cpp.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char MAGIC[8] = { 'R', 'T', 'S', 'P', 'A', 'K', '0', '1' };

struct PackedFile {
    std::string name;     // path inside the pak, forward slashes
    fs::path source;      // where it is on disk
    uint64_t size = 0;
    uint64_t offset = 0;  // filled in once the header size is known
};

// Writes an integer little-endian, matching the reader
template <typename T>
void writeInt(std::ostream& out, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.put(static_cast<char>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF));
    }
}

} // namespace

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
    const std::string prefix = root.filename().string();   // "data"

    std::vector<PackedFile> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        PackedFile f;
        f.name = prefix + "/" + fs::relative(entry.path(), root).generic_string();
        f.source = entry.path();
        f.size = (uint64_t)fs::file_size(entry.path());
        files.push_back(f);
    }
    std::sort(files.begin(), files.end(),
              [](const PackedFile& a, const PackedFile& b) { return a.name < b.name; });

    // The header comes first, so the byte offsets of the file contents depend on its size
    uint64_t offset = sizeof(MAGIC) + 4;
    for (const PackedFile& f : files) offset += 4 + f.name.size() + 8 + 8;
    for (PackedFile& f : files) {
        f.offset = offset;
        offset += f.size;
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
        writeInt(out, f.size);
    }
    for (const PackedFile& f : files) {
        std::ifstream in(f.source, std::ios::binary);
        if (!in) {
            std::cerr << "pack_data: could not read " << f.source.string() << "\n";
            return 1;
        }
        std::copy(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(),
                  std::ostreambuf_iterator<char>(out));
    }

    if (!out) {
        std::cerr << "pack_data: write failed for " << argv[2] << "\n";
        return 1;
    }
    std::cout << "pack_data: wrote " << files.size() << " files to " << argv[2] << "\n";
    return 0;
}
