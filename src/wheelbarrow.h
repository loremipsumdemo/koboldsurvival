#pragma once
#include "ecs.h"

// A Wheelbarrow is a prop (props.txt flag "wheelbarrow") that a unit occupies by taking hold of it: it stays on the
// ground in front of them, facing their way, and goes wherever they walk. Its store (props.txt's slots) is the prop's
// own, so what is in it weighs nothing on whoever pushes it. Attacking, or being attacked, makes them let go.
inline constexpr float WHEELBARROW_AHEAD = 0.75f;   // how far in front of its pusher it is
// A pulled one (props.txt flag "pulled", the Wagon) goes behind instead, this far, and slows whoever pulls it to
// PULLED_SPEED of their pace, unless they are a large animal (largeAnimal)
inline constexpr float PULLED_BEHIND = 1.5f;
inline constexpr float PULLED_SPEED = 0.1f;

// On the unit pushing a Wheelbarrow, and the way it faces (a unit's yaw: atan2 of its step's x and z)
struct Pushing {
    ObjectId barrow = NO_OBJECT;
    float yaw = 0.0f;
    float lastX = 0.0f, lastZ = 0.0f;
};

bool isWheelbarrow(World& w, ObjectId prop);
// The Wheelbarrow this unit pushes, or NO_OBJECT
ObjectId barrowOf(World& w, ObjectId u);
// Who pushes this Wheelbarrow, or NO_OBJECT
ObjectId pusherOf(World& w, ObjectId barrow);
// The unit takes hold of the Wheelbarrow, letting go of any other. False if someone else has it.
bool startPushing(World& w, ObjectId u, ObjectId barrow);
// The unit lets go of its Wheelbarrow where it is. logIt: a line in the log says so.
void stopPushing(World& w, ObjectId u, bool logIt = false);
// A fight: the attacker and the one attacked both let go
void pushingAttacked(World& w, ObjectId attacker, ObjectId target);
// Keeps each Wheelbarrow in front of its pusher (a pulled one behind), and lets go of it when they can't push it any more
void wheelbarrowSystem(World& w);
// How far ahead of whoever holds it a vehicle sits: below 0 is behind them, as a pulled one is
float vehicleOffset(World& w, ObjectId barrow);
// A Bear and the like (a mob drawn at least LARGE_ANIMAL_SCALE, or a boss): it pulls a Wagon at its own pace
inline constexpr float LARGE_ANIMAL_SCALE = 1.5f;
bool largeAnimal(World& w, ObjectId u);
// What the vehicle this unit holds does to its pace: PULLED_SPEED for a pulled one, unless it is a large animal; else 1
float vehiclePace(World& w, ObjectId u);
