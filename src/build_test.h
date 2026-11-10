#pragma once
// What the build being tested right now adds to the debug log and to the measuring run. All of it is in
// app_build_test.cpp, which is rewritten (or emptied) for the next build's testing; nothing else needs to change.

// What is being tested, in a few words: heads every [test] and [bench] line
extern const char* const BUILD_TEST_NAME;

// One mix of options the measuring run shows for a few seconds from each of its camera views
struct BenchCase {
    const char* name;
    bool clouds, shadows, varied, bands;
    int renderScale;
};
extern const BenchCase BUILD_TEST_CASES[];
extern const int BUILD_TEST_CASE_COUNT;
