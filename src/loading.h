#pragma once
#include "language.h"
#include <chrono>
#include <functional>
#include <string>

// The stages of a load, named under the loading bar with how many of their files are done ("Unit models 2/3")
class LoadStages {
public:
    using Progress = std::function<void(float, const std::string&)>;

    explicit LoadStages(Progress progress) : progress_(std::move(progress)) {}

    void stage(float p, const char* key, int count = 0) {
        key_ = key;
        count_ = count;
        show(p, 0);
    }
    // Drawing the loading screen waits for the monitor, far longer than most files take to load, so a step is only
    // drawn when the last drawing is this old. A new stage is always drawn: a long one may follow it.
    void step(float p, int done) {
        if (std::chrono::steady_clock::now() - shown_ < std::chrono::milliseconds(33)) return;
        show(p, done);
    }

private:
    void show(float p, int done) {
        if (!progress_) return;
        shown_ = std::chrono::steady_clock::now();
        std::string text = key_ ? lang(key_) : std::string();
        if (key_ && count_ > 0) text += " " + std::to_string(done) + "/" + std::to_string(count_);
        progress_(p, text);
    }

    Progress progress_;
    std::chrono::steady_clock::time_point shown_{};
    const char* key_ = nullptr;
    int count_ = 0;
};
