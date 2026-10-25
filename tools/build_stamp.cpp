// Writes the build stamp shown on the title screen, as a small C++ source file.
// Usage: build_stamp <output.cpp>
//
// The stamp is BUILD.YYYY.MM.DD.HH.MM.mmm in local time, read when this program runs. CMake runs it at the start of
// every build, so each build gets its own stamp. The output defines BUILD_STAMP, declared in src/build_info.h.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: build_stamp <output.cpp>\n");
        return 1;
    }

    // Seconds and milliseconds come from the same instant, so they always agree
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const int millis = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "BUILD.%04d.%02d.%02d.%02d.%02d.%03d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, millis);

    std::ofstream out(argv[1]);
    if (!out) {
        std::fprintf(stderr, "build_stamp: cannot write %s\n", argv[1]);
        return 1;
    }
    out << "// Written by tools/build_stamp.cpp on every build. Do not edit.\n";
    out << "extern const char* const BUILD_STAMP = \"" << stamp << "\";\n";
    return out ? 0 : 1;
}
