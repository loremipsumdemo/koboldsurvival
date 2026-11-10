#include "app.h"
#include "build_test.h"
#include "debug_log.h"

// ---------------------------------------------------------------------------------------------------------------------
// THE CURRENT BUILD'S TESTING. Rewrite this file for the next build; to test nothing, leave BUILD_TEST_NAME empty, the
// cases at one plain entry and debugLogBuildTest() empty.
//
// Now: culling everything in the world that is out of view, and drawing props, walls, foundations and units with a
// model as instanced copies, one draw call per model (and texture).
// ---------------------------------------------------------------------------------------------------------------------

const char* const BUILD_TEST_NAME = "culling and instanced models";

// name, clouds, cloud shadows, varied ground and water, distance bands, render scale
const BenchCase BUILD_TEST_CASES[] = {
    { "plain", true, true, true, true, 100 },
};
const int BUILD_TEST_CASE_COUNT = (int)(sizeof BUILD_TEST_CASES / sizeof BUILD_TEST_CASES[0]);

// Written after every [overview] line while playing
void App::debugLogBuildTest() {
    if (!BUILD_TEST_NAME[0]) return;
    debuglog::line("test", "%s: %s", BUILD_TEST_NAME, renderer.modelDrawText().c_str());
}
