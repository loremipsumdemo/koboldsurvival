#pragma once
#include <map>
#include <string>
#include <vector>
#include "ecs.h"
#include "rng.h"

// Patrons: a historical figure for each faction that claims land, no two the same. They are not units in the world: they speak for their
// faction in a line of dialogue (the Patron Dialogue banner at the top of the screen) whenever something happens between two factions, a call
// from the faction that did it and a response from the other. data/patrons.txt lists them; their names and descriptions are PATRON_nn_NAME and
// PATRON_nn_DESC in the language files, and their pictures are data/profile/patron/<picture>.

struct PatronInfo {
    int id = 0;
    std::string picture, mbti, nameKey, descKey;
};
bool loadPatrons(const std::string& text, std::string& err);
const std::vector<PatronInfo>& patrons();

// Which patron (an index into patrons()) each faction has. Saved.
struct Patrons {
    std::map<int, int> byFaction;
};
// The faction's patron, or -1 (a faction that claims no land, or none given yet)
int patronOf(World& w, int faction);
// Which faction has this patron, or -1
int patronHolder(World& w, int patron);
// A new game: every faction that claims land is given one. Yours is the one picked (an index), or a random one if that is -1; the others
// are drawn from those left.
void newPatrons(World& w, Rng& rng, int playerPick);
// A loaded game: a faction with none yet is given one of those left
void ensurePatrons(World& w, Rng& rng);

// What happened between two factions, for their patrons to speak of: the call is the patron of the one that did it (the caller), the response
// is the patron of the other (the responder).
enum class PatronEvent { Raid = 0, Hostile, Allied, Trade, Rescue, Attack, Kill, Trespass, Damage, Caged, Idle, Count };
// Said by the sim; the banner (app_patron.cpp) takes these, if both factions have a patron
struct PatronCue {
    PatronEvent event = PatronEvent::Raid;
    int caller = 0, responder = 0;
};
struct PatronTalk {
    std::vector<PatronCue> out;
};
void patronEvent(World& w, PatronEvent event, int caller, int responder);
// The language keys of an event's two lines (PATRON_<EVENT>_CALL, _RESPONSE)
const char* patronEventKey(PatronEvent event);
