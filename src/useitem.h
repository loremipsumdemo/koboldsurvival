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

enum class UseResult {
    Empty,
    NotUsable,
    Used
};

UseResult useStackInSlot(World& w, Entity unit, int slot);

UseResult useStackOn(World& w, Entity consumer, Entity target, int slot);

int restoreAmount(const ItemDef& def, UseEffect::Stat stat);
