#pragma once

// Emotes: small pictures over a unit's head that show what it is doing, from data/emote/emote_<id>.png (data/emoteinfo.txt
// lists them). A unit with several shows each in turn, EMOTE_CYCLE_MS apiece. A picture that is missing from the pak
// is drawn with emote_0.png, or as a pink square if that is missing too.
enum class EmoteId : int {
    None = 0,
    Mining = 1,        // working ore
    Woodcutting = 2,   // working a tree
    Cooking = 3,       // working a campfire
    Encumbered = 4,    // carrying ENCUMBERED_SHARE of its carry limit or more
    Sleeping = 5,      // lying on a bed or a bedroll
    Hungry = 6,        // hunger below 1
    Lockpicking = 7,   // picking a lock
    LockedIn = 8,     // in an occupiable prop that is locked (the Prisoner Cage), so it can't get out
    Herbalism = 9,     // gathering mushrooms
    Unconscious = 10,  // at 0 health, out cold (components.h, Unconscious)
    Dead = 11,         // a corpse: its Doom reached 100
    PlayingDead = 12, // awake, carried by a hostile, lying still until given an order
    CuePutDown = 13, // over a carried unit under the pointer whose carrier you have selected: right-click to put down
    CueTalk = 14,     // over an NPC under the pointer that can be talked to
    NoUpkeep = 15,    // over a vault with no upkeep items (logs) in it
    Decaying = 16,     // over a flag that is wearing down
};

constexpr unsigned EMOTE_CYCLE_MS = 1000;   // how long each emote shows when a unit has more than one
constexpr float ENCUMBERED_SHARE = 0.9f;    // carried weight, as a share of the carry limit, that shows Encumbered
// Every emote gives a little hop now and then, like the progress ring's when an item is finished, but lower
constexpr unsigned EMOTE_HOP_EVERY_MS = 2000;   // one hop this often
constexpr unsigned EMOTE_HOP_MS = 220;          // how long a hop takes, up and back down
constexpr float EMOTE_HOP_HEIGHT = 4.0f;        // how high it goes, in pixels at 720p (the ring's hop is 9)
