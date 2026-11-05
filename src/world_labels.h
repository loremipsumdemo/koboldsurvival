#pragma once
#include <string>
#include <vector>
#include "ecs.h"
#include "game_log.h"

// Text drawn in the world over a unit for a moment: damage numbers, and labels such as an item gained or the value of
// a trade. Any system can queue a label; the game takes them each frame.

constexpr float FLOAT_LIFE = 1.0f;   // seconds a damage number stays on screen

// A damage number over a unit's head, at the place the hit landed. age counts up from 0.
struct FloatingNumber {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int amount = 0;
    Tint tint = Tint::Gray;
    float age = 0.0f;
};

// A line of text over a unit for a moment, such as the value of a trade
constexpr float TRADE_LABEL_LIFE = 2.0f;     // seconds
struct TradeLabel {
    Entity unit = NULL_ENTITY;
    std::string text;
    float age = 0.0f;
    bool red = false;   // an item lost ("-Iron Ore"), drawn in red; the rest are yellow
};

// Labels any system can put over a unit, like the trade's: an item gained ("+Iron Ore", "+3x Iron Ore"), or what a
// used item did ("Health +15"). The game takes them each frame and shows them with its trade labels.
void queueUnitLabel(Entity unit, const std::string& text, bool red = false);
std::vector<TradeLabel> takeQueuedLabels();
void discardQueuedLabels();
// The label for count of an item gained: "+Iron Ore" for one, "+3x Iron Ore" for more
std::string itemGainLabel(int count, const std::string& itemName);
// The label for count of an item that left a unit's inventory: "-Iron Ore", "-3x Iron Ore". Queue it with red set.
std::string itemLossLabel(int count, const std::string& itemName);
// The same, for an item id, with the item's name in the language in use
std::string itemGainLabelFor(int count, int item);
std::string itemLossLabelFor(int count, int item);
