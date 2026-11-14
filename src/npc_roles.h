#pragma once
#include "ecs.h"
#include "geometry.h"

// The roles of an NPC faction's units, set by its Faction Rules (factions.h) once an hour:
//  - Fortified: guards the faction's Perimeter, as yours do (gates.h): keeps a gate, a Guard Post, or marches the walkways, and builds
//    only the Perimeter's pieces
//  - citizen: stays by the Altar, works, builds and hauls
//  - military: roams (the Roam task)
// The faction's Fortified share and military share (percent of its units) say how many of each; the rest are citizens. Each hour at
// most a few units of a faction change role, toward those shares, and only units that are idle.
void npcRolesHour(World& w, double gameHours);

// Put a unit into a role (home: the middle of its faction's claim)
void makeNpcCitizen(World& w, ObjectId u, Vec2 home);
void makeNpcFortified(World& w, ObjectId u, Vec2 home);
void makeNpcMilitary(World& w, ObjectId u);
