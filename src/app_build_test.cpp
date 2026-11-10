#include "app.h"
#include "build_test.h"
#include "debug_log.h"

// ---------------------------------------------------------------------------------------------------------------------
// THE CURRENT BUILD'S TESTING. Rewrite this file for the next build; to test nothing, leave BUILD_TEST_NAME empty, the
// cases at one plain entry and debugLogBuildTest() empty.
//
// Now: the Render scale setting, which draws the world smaller than the window and stretches it to fit. The question is
// how much each scale saves on a card that is short of pixels, and whether 75% (the default) holds 60 FPS.
// ---------------------------------------------------------------------------------------------------------------------

const char* const BUILD_TEST_NAME = "Render scale";

// name, clouds, cloud shadows, varied ground and water, distance bands, render scale
const BenchCase BUILD_TEST_CASES[] = {
    { "100% (before), everything on", true, true, true, true, 100 },
    { "75% (default), everything on", true, true, true, true, 75 },
    { "67%, everything on", true, true, true, true, 67 },
    { "50%, everything on", true, true, true, true, 50 },
    { "75%, no clouds or cloud shadows", false, false, true, true, 75 },
    { "75%, plain tiles, no clouds", false, false, false, true, 75 },
};
const int BUILD_TEST_CASE_COUNT = (int)(sizeof BUILD_TEST_CASES / sizeof BUILD_TEST_CASES[0]);

// Written after every [overview] line while playing
void App::debugLogBuildTest() {
    if (!BUILD_TEST_NAME[0]) return;
    debuglog::line("test", "%s: %s; %s", BUILD_TEST_NAME, renderer.renderScaleText().c_str(), renderer.surfaceBandsText().c_str());
}
