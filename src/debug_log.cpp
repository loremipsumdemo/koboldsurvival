#include "debug_log.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include "user_files.h"

namespace debuglog {
namespace {
std::FILE* file = nullptr;

std::FILE* openFile(const std::filesystem::path& path, const wchar_t* wideMode, const char* mode) {
#ifdef _WIN32
    (void)mode;
    return _wfopen(path.c_str(), wideMode);
#else
    (void)wideMode;
    return std::fopen(path.c_str(), mode);
#endif
}
}

void setEnabled(bool on) {
    if (on == (file != nullptr)) return;
    if (!on) {
        std::fclose(file);
        file = nullptr;
        return;
    }
    const std::filesystem::path path = std::filesystem::u8path(userFilesDir() + "logs.txt");
    std::error_code ec;
    const bool startOver = (long long)std::filesystem::file_size(path, ec) > LOG_MOST_BYTES && !ec;
    file = openFile(path, startOver ? L"wb" : L"ab", startOver ? "wb" : "ab");
}

bool enabled() { return file != nullptr; }

void line(const char* tag, const char* format, ...) {
    if (!file) return;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(file, "%s [%s] ", stamp, tag);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    // A crash must not lose what led up to it
    std::fflush(file);
}

}
