#pragma once

// Emotes: small pictures over a unit's head that show what it is doing, from data/emote/emote_<id>.png (data/emoteinfo.txt
// lists them). A unit with several shows each in turn, EMOTE_CYCLE_MS apiece. A picture that is missing from the pak
// is drawn with emote_0.png, or as a pink square if that is missing too.
enum EmoteId : int {
    EMOTE_NONE = 0,
    EMOTE_MINING = 1,        // working ore
    EMOTE_WOODCUTTING = 2,   // working a tree
    EMOTE_COOKING = 3,       // working a campfire
    EMOTE_ENCUMBERED = 4,    // carrying ENCUMBERED_SHARE of its carry limit or more
    EMOTE_SLEEPING = 5,      // lying on a bed or a bedroll
    EMOTE_HUNGRY = 6,        // hunger below 1
    EMOTE_LOCKPICKING = 7,   // picking a lock
    EMOTE_LOCKED_IN = 8,     // in an occupiable prop that is locked (the Prisoner Cage), so it can't get out
    EMOTE_HERBALISM = 9,     // gathering mushrooms
    EMOTE_UNCONSCIOUS = 10,  // at 0 health, out cold (components.h, Unconscious)
    EMOTE_DEAD = 11,         // a corpse: its Doom reached 100
    EMOTE_PLAYING_DEAD = 12, // awake, carried by a hostile, lying still until given an order
    EMOTE_CUE_PUT_DOWN = 13, // over a carried unit under the pointer whose carrier you have selected: right-click to put down
    EMOTE_CUE_TALK = 14,     // over an NPC under the pointer that can be talked to
};

constexpr unsigned EMOTE_CYCLE_MS = 1000;   // how long each emote shows when a unit has more than one
constexpr float ENCUMBERED_SHARE = 0.9f;    // carried weight, as a share of the carry limit, that shows Encumbered
// Every emote gives a little hop now and then, like the progress ring's when an item is finished, but lower
constexpr unsigned EMOTE_HOP_EVERY_MS = 2000;   // one hop this often
constexpr unsigned EMOTE_HOP_MS = 220;          // how long a hop takes, up and back down
constexpr float EMOTE_HOP_HEIGHT = 4.0f;        // how high it goes, in pixels at 720p (the ring's hop is 9)
