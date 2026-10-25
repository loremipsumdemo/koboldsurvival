#pragma once

#include "core/debug_options.hpp"
#include "render/gl_api.hpp"

namespace wm {


// GPU time per render pass, from GL_TIME_ELAPSED queries. The passes run one after
// another, so each pass has its own query, and each pass keeps a ring of queries so
// results can be read a few frames late. Reading never waits on the GPU.
class GpuTimers {
public:
    bool enabled = false;   // off costs nothing; the performance panel turns this on

    void Begin(Pass p);     // starts timing a pass; no-op when disabled
    void End();             // ends the pass started by Begin
    void NewFrame();        // call once per frame: collects finished results
    double Ms(Pass p) const { return ms_[static_cast<int>(p)]; }
    void Shutdown();

private:
    static constexpr int kSlots = 3;
    static constexpr int kPasses = static_cast<int>(Pass::Count);

    GLuint query_[kPasses][kSlots] = {};
    bool issued_[kPasses][kSlots] = {};   // query started and result not yet read
    double ms_[kPasses] = {};
    int frame_ = 0;
    int active_ = -1;                     // pass being timed, or -1
    bool ready_ = false;                  // queries have been generated
};

}  // namespace wm
