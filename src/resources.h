#pragma once
#include <ctime>
#include <optional>
#include <random>
#include <GL/glew.h>
#include "components.h"
#include "model.h"

// Textures and models loaded once at startup
struct Assets {
    GLuint floorTex = 0, playerTex = 0, keyTex = 0, doorTex = 0, backgroundTex = 0;
    TextTex glyphPlayer, glyphKey, glyphDoor, glyphWagon, glyphFloor;
    Model wagonModel;
};

struct Rng {
    std::mt19937 gen{ static_cast<std::mt19937::result_type>(std::time(nullptr)) };

    int range(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(gen);
    }
};

// Written by input, consumed by systems on the next frame
struct Commands {
    std::optional<GridPos> moveTo;
};
