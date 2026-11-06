#pragma once
#include <string>
#include <vector>
#include "ecs.h"
#include "game_log.h"

constexpr float FLOAT_LIFE = 1.0f;

struct FloatingNumber {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int amount = 0;
    Tint tint = Tint::Gray;
    float age = 0.0f;
};

constexpr float TRADE_LABEL_LIFE = 2.0f;
struct TradeLabel {
    Entity unit = NULL_ENTITY;
    std::string text;
    float age = 0.0f;
    bool red = false;
};

void queueUnitLabel(World& w, Entity unit, const std::string& text, bool red = false);
void queueFloatingNumber(World& w, const FloatingNumber& number);
std::string itemGainLabel(int count, const std::string& itemName);
std::string itemLossLabel(int count, const std::string& itemName);
std::string itemGainLabelFor(int count, int item);
std::string itemLossLabelFor(int count, int item);
