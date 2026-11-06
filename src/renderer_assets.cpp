#include "renderer_internal.h"

using namespace render_detail;

void Renderer::loadPictures(LoadStages& stages) {
    const int pictureCount = 1 + UNIT_SPRITE_COUNT;
    stages.stage(0.0f, "LOADING_PICTURES", pictureCount);
    titleLogo_ = loadOptionalImage("data/illustration/title.png");
    addMipmaps(titleLogo_);
    stages.step(0.03f, 1);
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/profile/profile_%02d.png", i);
        profilePics_[(size_t)i - 1] = loadOptionalImage(path);
        stages.step(0.03f + 0.02f * (float)i / (float)UNIT_SPRITE_COUNT, 1 + i);
    }
}

void Renderer::loadUnitModels(LoadStages& stages) {
    stages.stage(0.05f, "LOADING_UNIT_MODELS", UNIT_SPRITE_COUNT);
    struct Loaded {
        const unsigned char* bytes;
        size_t size;
        const Model* model;
    };
    std::vector<Loaded> loaded;
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/model/unit/unit_%02d.glb", i);
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        if (!pakFind(path, bytes, size)) {
            SDL_Log("Unit model %s not in data.pak: units with sprite %d are drawn as pink squares", path, i);
            stages.step(0.05f + 0.35f * (float)i / (float)UNIT_SPRITE_COUNT, i);
            continue;
        }
        const Model*& slot = unitModelFor_[(size_t)i - 1];
        for (const Loaded& l : loaded) {
            if (l.size == size && (l.bytes == bytes || std::memcmp(l.bytes, bytes, size) == 0)) {
                slot = l.model;
                break;
            }
        }
        if (!slot) {
            Model& m = unitModels_[(size_t)i - 1];
            if (loadDrawableModel(path, m, true)) {
                slot = &m;
                loaded.push_back({ bytes, size, &m });
            } else {
                SDL_Log("Unit model %s could not be read or has nothing to draw: units with sprite %d are drawn as pink squares",
                        path, i);
            }
        }
        stages.step(0.05f + 0.35f * (float)i / (float)UNIT_SPRITE_COUNT, i);
    }
}

void Renderer::loadMobArt(LoadStages& stages) {
    int mobModelCount = 0;
    for (int k = 1; k <= MOB_KIND_COUNT; ++k) mobModelCount += mobInfo((MobKind)k).model > 0 ? 1 : 0;
    stages.stage(0.4f, "LOADING_MOB_MODELS", mobModelCount);
    int mobModelsDone = 0;
    for (int k = 1; k <= MOB_KIND_COUNT; ++k) {
        const MobKind kind = (MobKind)k;
        const int n = mobInfo(kind).model;
        if (n <= 0) continue;
        ++mobModelsDone;
        char path[64];
        std::snprintf(path, sizeof(path), "data/model/mob/mob_%02d.glb", n);
        Model& m = mobModels_[(size_t)k - 1];
        if (!pakHas(path)) {
            SDL_Log("Mob model %s not in data.pak: the %s is drawn by its picture", path, lang(mobInfo(kind).nameKey).c_str());
        } else if (!loadDrawableModel(path, m, true)) {
            SDL_Log("Mob model %s could not be read or has nothing to draw: the %s is drawn by its picture", path,
                    lang(mobInfo(kind).nameKey).c_str());
        }
        stages.step(0.4f + 0.1f * (float)mobModelsDone / (float)std::max(1, mobModelCount), mobModelsDone);
    }
    stages.stage(0.5f, "LOADING_MOB_PICTURES", MOB_KIND_COUNT);
    mobSprites_.reserve(MOB_KIND_COUNT);
    for (int i = 1; i <= MOB_KIND_COUNT; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "data/mob/mob_%d.png", i);
        mobSprites_.push_back(loadOptionalImage(path));
        stages.step(0.5f + 0.05f * (float)i / (float)MOB_KIND_COUNT, i);
    }
}

void Renderer::makeTexels() {
    auto texel = [](TextTex& t, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t px[4] = { r, g, b, 255 };
        t = TextTex{ MakeRgbaTexture(1, 1, px, GL_NEAREST), 1.0f };
    };
    texel(white_, 255, 255, 255);
    texel(pink_, 255, 89, 191);
}

void Renderer::loadPropModels(LoadStages& stages) {
    stages.stage(0.57f, "LOADING_PROP_MODELS", PROP_KIND_COUNT);
    const char* standIn = "as a pink square";
    for (int k = 0; k < PROP_KIND_COUNT; ++k) {
        const PropKind kind = (PropKind)k;
        const PropInfo& info = propInfo(kind);
        if (!pakHas(info.file)) {
            SDL_Log("Prop model %s not in data.pak: the %s is drawn %s", info.file, lang(info.nameKey).c_str(), standIn);
        } else if (!loadDrawableModel(info.file, propModels_[(size_t)kind], false)) {
            SDL_Log("Prop model %s could not be read or has nothing to draw: the %s is drawn %s",
                    info.file, lang(info.nameKey).c_str(), standIn);
        }
        stages.step(0.58f + 0.4f * (float)(k + 1) / (float)PROP_KIND_COUNT, k + 1);
    }
}
