#include "app.h"
#include "build_test.h"
#include "debug_log.h"

// ---------------------------------------------------------------------------------------------------------------------
// THE CURRENT BUILD'S TESTING. Rewrite this file for the next build; to test nothing, leave BUILD_TEST_NAME empty, the
// cases at one plain entry and debugLogBuildTest() empty.
//
// Now: the performance pass for weak hardware: coarser models for far units (Model::lods), kept claims and wall tables and
// a grid for noticing in the simulation, fewer names and emotes, one draw for all fires, a ring buffer for quads, the sky
// and sea floor drawn after the ground, and 75% render scale by default on a card that shares memory with the processor.
// The [test] line adds the triangles sent a frame.
// ---------------------------------------------------------------------------------------------------------------------

const char* const BUILD_TEST_NAME = "performance pass";

// name, clouds, cloud shadows, varied ground and water, distance bands, render scale
const BenchCase BUILD_TEST_CASES[] = {
    { "100%, everything on", true, true, true, true, 100 },
    { "75%, everything on", true, true, true, true, 75 },
    { "50%, everything on", true, true, true, true, 50 },
    { "100%, no clouds, shadows or varied", false, false, false, true, 100 },
    { "100%, everything on, no bands", true, true, true, false, 100 },
};
const int BUILD_TEST_CASE_COUNT = (int)(sizeof BUILD_TEST_CASES / sizeof BUILD_TEST_CASES[0]);

// Written after every [overview] line while playing
void App::debugLogBuildTest() {
    if (!BUILD_TEST_NAME[0]) return;
    debuglog::line("test", "%s: %s", BUILD_TEST_NAME, renderer.modelDrawText().c_str());
}
