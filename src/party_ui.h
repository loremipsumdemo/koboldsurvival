#pragma once
#include <string>
#include <vector>
#include "party.h"
#include "ui_types.h"

enum class PartyTab : int { Identity = 0, Stats, Gear, Items };
inline constexpr int PARTY_TAB_COUNT = 4;

enum class DropKind { None, Sprite, Body, Gear, Item, MobKind };

struct DropEntry {
    std::string label;
    int value = 0;
};

struct PartyUi {
    int selected = 0;
    PartyTab tab = PartyTab::Identity;
    bool editingName = false;
    std::string composition;
    int stackSlot = 0;
    DropKind dropKind = DropKind::None;
    int dropSlot = 0;
    int dropScroll = 0;
    int listScroll = 0;
    bool listDrag = false;
    float listGrab = 0.0f;
    bool resizable = false;
    bool editable = false;
    bool mobKindEditable = false;
    float previewYaw = 0.0f;
    bool previewDrag = false;
};

inline constexpr int DROP_VISIBLE_ROWS = 10;

const PartyMember& pickedMember(const Party& p, const PartyUi& ui);
PartyMember& pickedMember(Party& p, const PartyUi& ui);

std::vector<DropEntry> dropdownEntries(const Party& p, const PartyUi& ui);

int clampDropScroll(int scroll, int count);

struct PartyPanels {
    float topH = 0.0f;
    Rect left;
    Rect center;
    Rect right;
    Rect bottom;
};
PartyPanels partyPanels(int W, int H);
Rect partyPreviewRect(int W, int H);

inline constexpr float PARTY_HINT_H = 130.0f;

int partyListVisible(int H);
int partyListMaxScroll(int count, int H);
struct PartyListBar {
    Rect track;
    Rect thumb;
};
PartyListBar partyListBar(int W, int H, int count, int scroll);
int partyListScrollAt(int W, int H, int count, float thumbTop);

std::vector<Button> layoutPartySetup(int W, int H, const Party& p, const PartyUi& ui,
                                     const std::vector<DropEntry>& drop, const std::string& patronName = "");

std::string partyMessage(const Party& p);

// The heading of a stat group
std::string groupTitle(StatGroup g);
