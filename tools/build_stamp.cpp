#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: build_stamp <output.cpp>\n");
        return 1;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "BUILD.%04d.%02d.%02d.%02d.%02d.%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec);

    std::ofstream out(argv[1]);
    if (!out) {
        std::fprintf(stderr, "build_stamp: cannot write %s\n", argv[1]);
        return 1;
    }
    out << "// Written by tools/build_stamp.cpp on every build. Do not edit.\n";
    out << "extern const char* const BUILD_STAMP = \"" << stamp << "\";\n";
    return out ? 0 : 1;
}
