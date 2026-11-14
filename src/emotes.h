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
    Hauling = 17,
    Building = 18,
    Socializing = 19,
    Sitting = 20,
    Playing = 21,
    Dancing = 22,
    Carrying = 23,
    Crafting = 24,
    Smelting = 25,
    Training = 26,
    Strength = 27,
    Clearing = 28,
    Fighting = 29,
    Fleeing = 30,
    Defending = 31,
    Following = 32,
    Talking = 33,
    Looting = 34,
    Stall = 35,
    Dead = 36,
    Carried = 37,
    Sharing = 38,
    TaskFailed = 39,
    Patrolling = 40,
    Gatekeeping = 41,
    Cleaning = 42,
};

constexpr unsigned EMOTE_CYCLE_MS = 1000;
constexpr float ENCUMBERED_SHARE = 0.9f;
constexpr unsigned EMOTE_HOP_EVERY_MS = 2000;
constexpr unsigned EMOTE_HOP_MS = 220;
constexpr float EMOTE_HOP_HEIGHT = 4.0f;
