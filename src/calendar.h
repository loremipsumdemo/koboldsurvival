#pragma once
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>
#include "language.h"

constexpr double SECONDS_PER_HOUR = 60.0 * 60.0;
constexpr double SECONDS_PER_DAY = 24.0 * SECONDS_PER_HOUR;
constexpr double DAY_REAL_SECONDS = 30.0 * 60.0;                                    // real seconds for one full day
constexpr double GAME_SECONDS_PER_REAL_SECOND = SECONDS_PER_DAY / DAY_REAL_SECONDS;

constexpr int DAYS_PER_MONTH = 30;
constexpr int MONTHS_PER_YEAR = 12;
constexpr int START_YEAR = 500;
constexpr double START_CLOCK_SECONDS = 6.0 * SECONDS_PER_HOUR;
static_assert(MONTHS_PER_YEAR <= 12, "month names are spelled out up to twelve");

struct CalendarDate {
    int year = START_YEAR;
    int month = 1;
    int day = 1;
};

struct ClockTime {
    int hour = 0;
    int minute = 0;
    int second = 0;
};

inline long long dayIndexOf(double seconds) {
    return (long long)std::floor(seconds / SECONDS_PER_DAY);
}

inline CalendarDate dateOf(double seconds) {
    const long long d = dayIndexOf(seconds);
    CalendarDate c;
    c.day = (int)(d % DAYS_PER_MONTH) + 1;
    c.month = (int)((d / DAYS_PER_MONTH) % MONTHS_PER_YEAR) + 1;
    c.year = START_YEAR + (int)(d / (DAYS_PER_MONTH * MONTHS_PER_YEAR));
    return c;
}

inline ClockTime clockOf(double seconds) {
    double inDay = seconds - (double)dayIndexOf(seconds) * SECONDS_PER_DAY;
    long long t = (long long)std::floor(inDay);
    if (t < 0) t = 0;
    if (t > 86399) t = 86399;
    ClockTime c;
    c.hour = (int)(t / 3600);
    c.minute = (int)((t / 60) % 60);
    c.second = (int)(t % 60);
    return c;
}

inline float timeOfDayMinutes(double seconds) {
    return (float)((seconds - (double)dayIndexOf(seconds) * SECONDS_PER_DAY) / 60.0);
}

inline std::string ordinalWord(int n) {
    if (n < 1 || n > 12) return std::to_string(n);
    return lang("MISC_ORD_" + std::to_string(n));
}

inline std::string monthName(int month) {
    return langf("UILABEL_MONTH_NAME", { ordinalWord(month) });
}

inline std::string formatDate(double seconds) {
    const CalendarDate d = dateOf(seconds);
    std::string s = langf("UILABEL_DATE_LINE", { std::to_string(d.day), monthName(d.month), std::to_string(d.year) });
    for (char& ch : s) {
        if (ch >= 'a' && ch <= 'z') ch = (char)std::toupper((unsigned char)ch);
    }
    return s;
}

inline std::string formatTime(double seconds) {
    const ClockTime c = clockOf(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", c.hour, c.minute);
    return buf;
}

inline std::string formatYmd(double seconds) {
    const CalendarDate d = dateOf(seconds);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}

inline int advanceClock(double& seconds, double realSeconds) {
    if (realSeconds <= 0.0) return 0;
    const long long before = (long long)std::floor(seconds / SECONDS_PER_HOUR);
    seconds += realSeconds * GAME_SECONDS_PER_REAL_SECOND;
    const long long after = (long long)std::floor(seconds / SECONDS_PER_HOUR);
    return (int)(after - before);
}
