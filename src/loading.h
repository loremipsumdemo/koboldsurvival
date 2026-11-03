#pragma once
#include "language.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <functional>
#include <string>

// The stages of a load, for the loading screen and log.txt. Each stage is a kind of asset (a language key, such as
// LOADING_UNIT_MODELS), named under the bar with how many of its files are done ("Unit models 2/3"). When a stage
// ends, log.txt gets how long it took.
class LoadStages {
public:
    using Progress = std::function<void(float, const std::string&)>;

    LoadStages(const char* what, Progress progress) : what_(what), progress_(std::move(progress)), begun_(now()) {}
    ~LoadStages() { finish(); }

    // A new stage starts: the last one's time goes to the log
    void stage(float p, const char* key, int count = 0) {
        end();
        key_ = key;
        count_ = count;
        start_ = now();
        show(p, 0);
    }
    // done of the stage's count are loaded now
    void step(float p, int done) { show(p, done); }
    // The last stage ends, and the whole load's time goes to the log
    void finish() {
        if (finished_) return;
        end();
        finished_ = true;
        SDL_Log("%s loaded in %.0f ms", what_, ms(begun_));
    }

private:
    using Clock = std::chrono::steady_clock;
    static Clock::time_point now() { return Clock::now(); }
    static double ms(Clock::time_point since) {
        return std::chrono::duration<double, std::milli>(now() - since).count();
    }
    void show(float p, int done) {
        if (!progress_) return;
        std::string text = key_ ? lang(key_) : std::string();
        if (key_ && count_ > 0) text += " " + std::to_string(done) + "/" + std::to_string(count_);
        progress_(p, text);
    }
    void end() {
        if (!key_) return;
        if (count_ > 0) SDL_Log("%s: %s (%d) in %.0f ms", what_, lang(key_).c_str(), count_, ms(start_));
        else SDL_Log("%s: %s in %.0f ms", what_, lang(key_).c_str(), ms(start_));
        key_ = nullptr;
    }

    const char* what_;
    Progress progress_;
    Clock::time_point begun_, start_{};
    const char* key_ = nullptr;
    int count_ = 0;
    bool finished_ = false;
};
