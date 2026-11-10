#pragma once
// The debug log: logs.txt in the user files folder, written only while Debug logging is on in the settings. Every line
// starts with the time and a tag saying what it is about:
//     2026-11-09 19:29:09 [session] ...
// New lines go on the end; a file that has grown past LOG_MOST_BYTES is started over when the game opens it.
namespace debuglog {

inline constexpr long long LOG_MOST_BYTES = 2 * 1024 * 1024;

void setEnabled(bool on);
bool enabled();
// printf-style. Does nothing while the log is off.
void line(const char* tag, const char* format, ...);

}
