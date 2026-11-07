#pragma once
#include <string>
#include <vector>
#include "ecs.h"

// Your faction's dead come back as Ghosts. Each wants one sacrifice of bones (any kind) at your Altar. A Ghost left
// unappeased for GHOST_GRACE_HOURS of game time haunts the whole faction: every unit of yours is Haunted until it is appeased.
inline constexpr double GHOST_GRACE_HOURS = 24.0;
inline constexpr float HAUNTED_ATTACK = 0.75f;   // share of attack a Haunted unit keeps
inline constexpr float HAUNTED_SPEED = 0.85f;    // share of walking speed

// Only what the Graveyard shows: a name and a picture
struct Ghost {
    std::string name;
    int sprite = 1;
    double hours = 0.0;      // game hours since death, while unappeased
    bool appeased = false;
};

// In the World's resources
struct Graveyard {
    std::vector<Ghost> ghosts;
    bool haunted = false;
};

bool isBones(int item);
// One of your units has died: it joins the Graveyard
void recordGhost(World& w, ObjectId u);
int unappeasedGhosts(World& w);
// Ages the unappeased Ghosts and works out whether your faction is Haunted, and says so when that changes
void ghostSystem(World& w, double gameSeconds);
bool hauntedUnit(World& w, ObjectId u);
// How many bones the Altar holds
int bonesIn(World& w, ObjectId altar);
// Sacrifices one of the Altar's bones to that Ghost. False if it has none, or the Ghost is already appeased.
bool appeaseGhost(World& w, ObjectId altar, size_t ghost);
