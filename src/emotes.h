#pragma once

enum class EmoteId : int {
    None = 0,
    Mining = 1,
    Woodcutting = 2,
    Cooking = 3,
    Encumbered = 4,
    Sleeping = 5,
    Hungry = 6,
    Lockpicking = 7,
    LockedIn = 8,
    Herbalism = 9,
    Unconscious = 10,
    PlayingDead = 12,
    CuePutDown = 13,
    CueTalk = 14,
    NoUpkeep = 15,
    Decaying = 16,
};

constexpr unsigned EMOTE_CYCLE_MS = 1000;
constexpr float ENCUMBERED_SHARE = 0.9f;
constexpr unsigned EMOTE_HOP_EVERY_MS = 2000;
constexpr unsigned EMOTE_HOP_MS = 220;
constexpr float EMOTE_HOP_HEIGHT = 4.0f;
