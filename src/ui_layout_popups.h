#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "components.h"
#include "items.h"
#include "props.h"
#include "ui_types.h"
#include "ui_layout_panels.h"

struct DialogueLayout {
    Rect box;
    std::vector<Rect> lines;
    std::vector<Rect> options;
    Rect end;
};
DialogueLayout layoutDialogue(int W, int H, int lineCount, int optionCount);

constexpr int TRADE_SLOTS = 12;
struct TradeLayout {
    Rect interaction;
    Rect interBar;
    Rect npcGrid;
    std::vector<Rect> npcSlots;
    Rect tradePanel;
    Rect tradeBar;
    Rect theirName;
    Rect yourName;
    Rect theirBox;
    Rect yourBox;
    std::vector<Rect> theirCells, yourCells;
    Rect theirTotal;
    Rect yourTotal;
    Rect valueBar;
    Rect statusLine;
    Rect cancelBtn;
    Rect tradeBtn;
};
TradeLayout layoutTrade(int W, int H, float top = 0.0f);

struct StorageLayout {
    Rect panel;
    Rect bar;
    Rect name;
    Rect heading;
    Rect grid;
    std::vector<Rect> slots;
    bool output = false;
    Rect outHeading;
    Rect outGrid;
    std::vector<Rect> outSlots;
    Rect status;
    Rect hint;
    Rect closeBtn;
    bool lockToggle = false;
    Rect lockBtn;
};
struct LootLayout {
    Rect panel;
    Rect bar;
    Rect name;
    Rect gearHeading;
    Rect gearGrid;
    std::vector<Rect> gear;
    Rect invHeading;
    Rect invGrid;
    std::vector<Rect> slots;
    Rect hint;
    Rect takeAllBtn;
    Rect closeBtn;
};
LootLayout layoutLoot(int W, int H);

StorageLayout layoutStorage(int W, int H, bool output = false, bool lockToggle = false);

struct TradeStatus {
    bool ok = false;
    std::string text;
    float progress = 0.0f;
};

float tradeFeePercent(int charisma);

int requiredOffer(int npcValue, int charisma);

int tradeValueOf(const std::vector<TradeGhost>& ghosts);

struct QuantityLayout {
    Rect box;
    Rect title;
    Rect countBox;
    Rect minus100, minus10, minus1;
    Rect plus1, plus10, plus100;
    Rect single, max;
    Rect confirm, cancel;
};

struct OptimizeAllLayout {
    Rect box;
    Rect title;
    Rect text;
    Rect sliderLabel;
    Rect track;
    std::array<Rect, 3> toggles{};
    Rect confirm, cancel;
};
OptimizeAllLayout layoutOptimizeAll(int W, int H);

constexpr unsigned long long DISBAND_HOLD_MS = 3000;
struct DisbandLayout {
    Rect box;
    Rect title;
    Rect portrait;
    Rect info;
    Rect text;
    Rect hint;
    Rect disband, cancel;
};
constexpr int DISBAND_INFO_LINES = 3;
DisbandLayout layoutDisband(int W, int H);

QuantityLayout layoutQuantity(int W, int H);

struct PercentPromptLayout {
    Rect box;
    Rect input;
    Rect ok;
    Rect cancel;
    float titleY = 0.0f, rangeY = 0.0f, errorY = 0.0f;
};
PercentPromptLayout layoutPercentPrompt(int W, int H);

struct ItemMenuLayout {
    Rect box;
    std::vector<Rect> buttons;
};

ItemMenuLayout layoutItemMenu(float x, float y, int buttonCount, float rowH, int W, int H);

int itemMenuButtonAt(const ItemMenuLayout& L, float x, float y);
