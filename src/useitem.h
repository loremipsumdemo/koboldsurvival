#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "items.h"

struct UseEffect {
    enum class Stat { Health, Hunger, Attack };
    Stat stat = Stat::Health;
    int amount = 0;
    float seconds = 0.0f;
};

bool parseUseScript(const std::string& script, std::vector<UseEffect>& out, std::string& err);

struct UseTarget {
    Needs* needs = nullptr;
    Combat* combat = nullptr;
    float healScale = 1.0f;
};

int applyUse(const std::vector<UseEffect>& effects, const UseTarget& target);

// A unit that has just eaten, drunk or used a medicine: the renderer plays its consume animation while this lasts (behaviorSystem counts it down)
inline constexpr float CONSUME_SECONDS = 2.0f;
struct Consuming {
    float left = CONSUME_SECONDS;
};

enum class UseResult {
    Empty,
    NotUsable,
    Used
};

UseResult useStackInSlot(World& w, ObjectId unit, int slot);

UseResult useStackOn(World& w, ObjectId consumer, ObjectId target, int slot);

int restoreAmount(const ItemDef& def, UseEffect::Stat stat);
