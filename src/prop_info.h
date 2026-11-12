#pragma once
#include "sounds.h"
#include "props.h"
#include <string>
#include <vector>
#include "ecs.h"

std::vector<std::string> altarInfoLines(World& w, ObjectId e);

// What the hover box says about a prop. The first line is its name. An Altar's last line is its faction's Motivation, which the hover
// box draws as a bar (altarMotivationBar).
std::vector<std::string> propInfoLines(World& w, ObjectId e);

// How full the bar on an Altar's last hover line is, where the threshold's mark is (both 0 to 1), and whether it is over the threshold
struct MotivationBarInfo {
    float fill = 0.0f, mark = 0.0f;
    bool fast = false;
};
bool altarMotivationBar(World& w, ObjectId e, MotivationBarInfo& out);

// The sound a prop makes while worked, and when it finishes an item. Sfx::Count: none.
