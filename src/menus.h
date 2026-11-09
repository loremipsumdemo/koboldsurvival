#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include "game_tables.h"
#include "level.h"
#include "savegame.h"
#include "ui_types.h"

constexpr size_t MIN_SAVE_ROWS = 5;
constexpr size_t MAX_SAVE_ROWS = 10;

constexpr float COL_TYPE = 0.54f;
constexpr float COL_TIME = 0.70f;

inline void pageUp(size_t& offset, size_t page) { offset = offset > page ? offset - page : 0; }
inline void pageDown(size_t& offset, size_t count, size_t page) {
    offset = std::min(offset + page, count > page ? count - page : 0);
}

template <typename Key>
inline void pickSortColumn(Key& column, Key picked, bool& ascending) {
    ascending = column == picked ? !ascending : true;
    column = picked;
}

enum class SortKey { Name, Time };

enum class Ask { None, Overwrite, LoadUnsaved, Delete, LeaveUnsaved };

std::string trimmedName(const std::string& s);
void popUtf8Char(std::string& s);
std::optional<SaveSummary> findSaveByName(const std::vector<SaveSummary>& saves, const std::string& name);
void sortSaves(std::vector<SaveSummary>& saves, SortKey key, bool ascending);
std::optional<MapChoice> findMap(const std::vector<MapChoice>& maps, const std::string& source, const std::string& stem);

size_t visibleSaveRows(int H);

// The save list on the left; on the right the picked save's Load (or Save) button, its picture, and its details
struct PanelGeometry {
    Rect box;
    float pad;
    float listX, listW;
    Rect nameBox;
    Rect sideBtn;
    Rect preview;
    float detailsY;
    float detailsW;
    float headerY;
    float headerH;
    Rect aboveBand;
    float listTop;
    Rect belowBand;
    float closeY;
    float btnH;
    float rowH;
    float rowPitch;
};
PanelGeometry panelGeometry(int W, int H);

struct DialogGeometry {
    Rect box;
    Rect yes;
    Rect no;
};
DialogGeometry questionGeometry(int W, int H);

// canConfirm: whether the Load or Save button at the top of the right side can be pressed
std::vector<Button> layoutPanel(int W, int H, bool saveMode, Ask ask,
                                const std::vector<SaveSummary>& saves, size_t listOffset, bool fromTitle, bool canConfirm);
std::vector<Button> layoutButtons(Screen screen, int W, int H, const std::vector<SaveSummary>& saves);

Soundtrack soundtrackFor(Screen screen);

std::string gameTitle();
const std::vector<std::string>& messageOfTheDay();
