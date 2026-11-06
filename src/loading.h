#pragma once
#include "language.h"
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
        step(p, 0);
    }
    void step(float p, int done) {
        if (!progress_) return;
        std::string text = key_ ? lang(key_) : std::string();
        if (key_ && count_ > 0) text += " " + std::to_string(done) + "/" + std::to_string(count_);
        progress_(p, text);
    }

private:
    Progress progress_;
    const char* key_ = nullptr;
    int count_ = 0;
};
