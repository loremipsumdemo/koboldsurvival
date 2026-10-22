#include "render/gpu_timer.hpp"

#include <cstdint>

namespace wm {


void GpuTimers::Begin(Pass p) {
    if (!enabled || active_ >= 0) return;
    if (!ready_) {
        glGenQueries(kPasses * kSlots, &query_[0][0]);
        ready_ = true;
    }

    const int pi = static_cast<int>(p);
    const int slot = frame_ % kSlots;
    if (issued_[pi][slot]) return;   // this query's last result is still pending
    glBeginQuery(GL_TIME_ELAPSED, query_[pi][slot]);
    issued_[pi][slot] = true;
    active_ = pi;
}

void GpuTimers::End() {
    if (active_ < 0) return;
    glEndQuery(GL_TIME_ELAPSED);
    active_ = -1;
}

void GpuTimers::NewFrame() {
    ++frame_;
    if (!ready_) return;

    // The slot read here is the one used kSlots frames ago, so its result is
    // normally ready by now. If it is not, the old value stays for another frame.
    const int slot = frame_ % kSlots;
    for (int pi = 0; pi < kPasses; ++pi) {
        if (!issued_[pi][slot]) continue;
        GLint available = 0;
        glGetQueryObjectiv(query_[pi][slot], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) continue;
        std::uint64_t ns = 0;
        glGetQueryObjectui64v(query_[pi][slot], GL_QUERY_RESULT, &ns);
        ms_[pi] = static_cast<double>(ns) / 1.0e6;
        issued_[pi][slot] = false;
    }
}

void GpuTimers::Shutdown() {
    if (ready_) {
        glDeleteQueries(kPasses * kSlots, &query_[0][0]);
        ready_ = false;
    }
    active_ = -1;
}

}  // namespace wm
