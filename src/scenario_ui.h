#pragma once
#include <string>
#include <vector>
#include "scenario.h"
#include "ui_types.h"

// Add Custom Scenario (app_scenario.cpp): who an edit is for, which list of rows is showing, and the question over the screen

// Whom an edit is for: every faction (yours too), the ones that run themselves (NPC), yours alone, or one colored faction by its slot on the loop
enum class ScenarioScope : int { All = 0, Npc, Player, Faction, Count };
// The lists of rows: how many units, which animals, the camp and its props, and the faction rules
enum class ScenarioTab : int { Units = 0, Mobs, Props, Rules, Count };
// A question over the screen: delete a saved scenario, or write over one
enum class ScenarioAsk : int { None = 0, Delete, Overwrite };

struct ScenarioEditUi {
    Scenario scenario;                 // the one being made
    std::string name;                  // its file's name, as typed
    std::string loadedName;            // the file it was opened from, empty for one not saved yet
    bool editingName = false, editingAbout = false;
    ScenarioScope scope = ScenarioScope::All;
    int slot = 0;                      // for the Faction scope: which faction, by its slot on the loop
    ScenarioTab tab = ScenarioTab::Units;
    int scroll = 0;                    // the first row showing of the list
    std::vector<std::string> files;    // the scenarios saved in MAP/SCENARIO
    int fileScroll = 0;
    ScenarioAsk ask = ScenarioAsk::None;
    std::string askName;               // the file the question is about
    std::string savedText;             // what the scenario was when it was last saved or opened, to tell whether it has changed
};

struct ScenarioEditLayout {
    Rect panel, title, nameBox, aboutBox, slotDown, slotBox, slotUp, list, info, save, back;
    Rect scopes[4], tabs[4];
    struct Line {
        Rect box, label, down, value, up, reset;
    };
    std::vector<Line> lines;           // the rows showing
    int first = 0, shown = 0;
    RowBar bar;
    Rect filesBox, fresh;
    std::vector<Rect> fileRows, fileDelete;
    int fileFirst = 0;
    RowBar fileBar;
};
ScenarioEditLayout layoutScenarioEdit(int W, int H, bool factionScope, int rowCount, int rowScroll, int fileCount, int fileScroll);
