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
        const std::string file = "data/model/" + unitModelFile(i);
        const char* path = file.c_str();
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
    mobModels_.resize((size_t)mobKindCount());
    int mobModelCount = 0;
    for (int k = 1; k <= mobKindCount(); ++k) mobModelCount += mobInfo((MobKind)k).model.empty() ? 0 : 1;
    stages.stage(0.4f, "LOADING_MOB_MODELS", mobModelCount);
    int mobModelsDone = 0;
    for (int k = 1; k <= mobKindCount(); ++k) {
        const MobKind kind = (MobKind)k;
        if (mobInfo(kind).model.empty()) continue;
        ++mobModelsDone;
        const std::string file = "data/model/" + mobInfo(kind).model;
        const char* path = file.c_str();
        Model& m = mobModels_[(size_t)k - 1];
        if (!pakHas(path)) {
            SDL_Log("Mob model %s not in data.pak: the %s is drawn by its picture", path, lang(mobInfo(kind).nameKey).c_str());
        } else if (!loadDrawableModel(path, m, true)) {
            SDL_Log("Mob model %s could not be read or has nothing to draw: the %s is drawn by its picture", path,
                    lang(mobInfo(kind).nameKey).c_str());
        }
        stages.step(0.4f + 0.1f * (float)mobModelsDone / (float)std::max(1, mobModelCount), mobModelsDone);
    }
    stages.stage(0.5f, "LOADING_MOB_PICTURES", mobKindCount());
    mobSprites_.reserve(mobKindCount());
    for (int i = 1; i <= mobKindCount(); ++i) {
        const std::string path = "data/mob/" + mobInfo((MobKind)i).picture;
        mobSprites_.push_back(loadOptionalImage(path.c_str()));
        stages.step(0.5f + 0.05f * (float)i / (float)mobKindCount(), i);
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
    propModels_.resize((size_t)propKindCount());
    stages.stage(0.57f, "LOADING_PROP_MODELS", propKindCount());
    wallShapes_.assign((size_t)propKindCount(), {});
    materialModels_.assign((size_t)propKindCount(), {});
    const char* standIn = "as a pink square";
    for (int k = 0; k < propKindCount(); ++k) {
        const PropKind kind = (PropKind)k;
        const PropInfo& info = propInfo(kind);
        const std::string file = "data/model/" + propModelFile(kind);
        if (!pakHas(file)) {
            SDL_Log("Prop model %s not in data.pak: the %s is drawn %s", file.c_str(), lang(info.nameKey).c_str(), standIn);
        } else if (!loadDrawableModel(file.c_str(), propModels_[(size_t)kind], false)) {
            SDL_Log("Prop model %s could not be read or has nothing to draw: the %s is drawn %s",
                    file.c_str(), lang(info.nameKey).c_str(), standIn);
        }
        if (info.wallRule) {
            // Its other shapes sit beside its model: model_45.glb has model_45_end.glb and so on
            static const char* const SHAPES[5] = { "_end", "_straight", "_corner", "_tee", "_cross" };
            const std::string stem = file.size() > 4 ? file.substr(0, file.size() - 4) : file;
            for (int s = 0; s < 5; ++s) {
                const std::string shaped = stem + SHAPES[s] + ".glb";
                if (!pakHas(shaped) || !loadDrawableModel(shaped.c_str(), wallShapes_[(size_t)k][(size_t)s], false)) {
                    SDL_Log("Wall model %s is missing: that shape is drawn as the plain post", shaped.c_str());
                }
            }
        }
        if (info.foundation || info.wallRule || info.door) {
            // Iron and crystal beside the plain one: model_45_iron.glb, model_45_iron_straight.glb and so on
            static const char* const MATS[2] = { "_iron", "_crystal" };
            static const char* const SHAPES[6] = { "", "_end", "_straight", "_corner", "_tee", "_cross" };
            const std::string stem = file.size() > 4 ? file.substr(0, file.size() - 4) : file;
            for (int m = 0; m < 2; ++m) {
                for (int sh = 0; sh < (info.wallRule ? 6 : 1); ++sh) {
                    const std::string f = stem + MATS[m] + SHAPES[sh] + ".glb";
                    if (pakHas(f)) loadDrawableModel(f.c_str(), materialModels_[(size_t)k][(size_t)m][(size_t)sh], false);
                }
            }
        }
        stages.step(0.58f + 0.4f * (float)(k + 1) / (float)propKindCount(), k + 1);
    }
}
