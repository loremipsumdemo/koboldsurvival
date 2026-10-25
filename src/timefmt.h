#pragma once
#include <cstdio>
#include <ctime>
#include <string>

// ISO 8601 style, 24-hour local time, with milliseconds: "2026-10-17_08:24:05.123".
// ms is milliseconds since 1970. Shared by the saves and the game world list.
inline std::string timeString(long long ms) {
    std::time_t t = (std::time_t)(ms / 1000);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%d_%H:%M:%S", &tmv);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s.%03lld", date, ms % 1000);
    return buf;
}
