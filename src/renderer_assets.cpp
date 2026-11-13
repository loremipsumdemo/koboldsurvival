#include "renderer_internal.h"
#include "building.h"

using namespace render_detail;

void Renderer::loadPictures(LoadStages& stages) {
    const int pictureCount = 1 + UNIT_SPRITE_COUNT;
    stages.stage(0.0f, "LOADING_PICTURES", pictureCount);
    titleLogo_ = loadOptionalImage("data/illustration/title.png");
    addMipmaps(titleLogo_);
    stages.step(0.03f, 1);
    for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) {
        const std::string path = "data/profile/" + unitModelKey(i) + ".png";
        profilePics_[(size_t)i - 1] = loadOptionalImage(path.c_str());
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
        // A mob drawn by its model has no picture (none). Its profile picture is data/profile/<its key>.png (bear.png).
        const MobInfo& info = mobInfo((MobKind)i);
        const std::string path = "data/mob/" + info.picture;
        mobSprites_.push_back(info.picture == "none" ? TextTex{} : loadOptionalImage(path.c_str()));
        const std::string profile = "data/profile/" + fileSlug(info.key) + ".png";
        mobProfiles_.push_back(loadOptionalImage(profile.c_str()));
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
    // A soft round spot for flames and their glow: white, fading out from the middle
    constexpr int SIDE = 48;
    std::vector<uint8_t> px((size_t)SIDE * SIDE * 4);
    for (int y = 0; y < SIDE; ++y) {
        for (int x = 0; x < SIDE; ++x) {
            const float dx = ((float)x + 0.5f) / SIDE * 2.0f - 1.0f, dy = ((float)y + 0.5f) / SIDE * 2.0f - 1.0f;
            const float d = std::min(1.0f, std::sqrt(dx * dx + dy * dy));
            const float a = (1.0f - d) * (1.0f - d);
            uint8_t* p = &px[((size_t)y * SIDE + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255.0f);
        }
    }
    glow_ = TextTex{ MakeRgbaTexture(SIDE, SIDE, px.data(), GL_LINEAR), 1.0f };
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
        const bool shaped = info.wallRule || info.upperFloor || info.solid;   // walls, floors and Perimeter Walls take a shape from what is beside them
        if (shaped) {
            // Its other shapes sit beside its model: wall_wood.glb has wall_wood_end.glb and so on
            static const char* const SHAPES[5] = { "_end", "_straight", "_corner", "_tee", "_cross" };
            const std::string stem = file.size() > 4 ? file.substr(0, file.size() - 4) : file;
            for (int s = 0; s < 5; ++s) {
                const std::string shaped = stem + SHAPES[s] + ".glb";
                if (!pakHas(shaped) || !loadDrawableModel(shaped.c_str(), wallShapes_[(size_t)k][(size_t)s], false)) {
                    SDL_Log("Wall model %s is missing: that shape is drawn as the plain post", shaped.c_str());
                }
            }
        }
        if (takesMaterial(kind)) {
            // The other materials beside the plain (wood) one, in BuildMaterial's order after Wood:
            // wall_wood.glb has wall_metal.glb, wall_metal_straight.glb and so on
            static const char* const MATS[MATERIAL_MODELS] = { "_metal", "_crystal", "_stone" };
            static const char* const SHAPES[6] = { "", "_end", "_straight", "_corner", "_tee", "_cross" };
            std::string stem = file.size() > 4 ? file.substr(0, file.size() - 4) : file;
            if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_wood") == 0) stem.erase(stem.size() - 5);
            for (int m = 0; m < MATERIAL_MODELS; ++m) {
                for (int sh = 0; sh < (shaped ? 6 : 1); ++sh) {
                    const std::string f = stem + MATS[m] + SHAPES[sh] + ".glb";
                    if (pakHas(f)) loadDrawableModel(f.c_str(), materialModels_[(size_t)k][(size_t)m][(size_t)sh], false);
                }
            }
        }
        if (info.perimeter && !info.solid) {
            // A Gate's open model, beside each of its closed ones: perimeter_gate_wood_open.glb, perimeter_gate_metal_open.glb. The
            // wood one waits in wallShapes_'s first place, the others in each material's second.
            static const char* const MATS[MATERIAL_MODELS] = { "_metal", "_crystal", "_stone" };
            std::string stem = file.size() > 4 ? file.substr(0, file.size() - 4) : file;
            if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_wood") == 0) stem.erase(stem.size() - 5);
            const std::string wood = stem + "_wood_open.glb";
            if (pakHas(wood)) loadDrawableModel(wood.c_str(), wallShapes_[(size_t)k][0], false);
            for (int m = 0; m < MATERIAL_MODELS; ++m) {
                const std::string f = stem + MATS[m] + "_open.glb";
                if (pakHas(f)) loadDrawableModel(f.c_str(), materialModels_[(size_t)k][(size_t)m][1], false);
            }
        }
        stages.step(0.58f + 0.4f * (float)(k + 1) / (float)propKindCount(), k + 1);
    }
    {
        // Stairs' step in each material, by BuildMaterial
        static const char* const STEPS[4] = { "data/model/stair_step_wood.glb", "data/model/stair_step_metal.glb",
                                              "data/model/stair_step_crystal.glb", "data/model/stair_step_stone.glb" };
        for (int m = 0; m < 4; ++m) {
            if (pakHas(STEPS[m])) loadDrawableModel(STEPS[m], stairSteps_[(size_t)m], false);
        }
    }

    // The building pieces' surfaces (tools/make_building_textures.py), laid over them in world space
    auto surface = [](const char* path) {
        TextTex t = loadOptionalImage(path);
        if (!t.tex) {
            SDL_Log("%s is missing: building pieces of that material are drawn plain", path);
            return t;
        }
        addMipmaps(t);
        glBindTexture(GL_TEXTURE_2D, t.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        return t;
    };
    surfaces_[(size_t)BuildMaterial::Wood] = surface("data/texture/wood_grain.png");
    surfaces_[(size_t)BuildMaterial::Stone] = surface("data/texture/stone_blocks.png");
    surfaces_[(size_t)BuildMaterial::Iron] = surface("data/texture/metal_plates.png");
    surfaces_[(size_t)BuildMaterial::Crystal] = surface("data/texture/crystal_facets.png");
    const GLuint wood = surfaces_[(size_t)BuildMaterial::Wood].tex;
    for (int k = 0; k < propKindCount(); ++k) {
        const PropInfo& info = propInfo((PropKind)k);
        // Every piece built on a foundation is wood unless made of a material: ladders and stairs too
        if (!takesMaterial((PropKind)k) && !info.ladder && !info.stairs) continue;
        propModels_[(size_t)k].surface = wood;
        for (Model& m : wallShapes_[(size_t)k]) m.surface = wood;
        for (int m = 0; m < MATERIAL_MODELS; ++m) {
            const GLuint tex = surfaces_[(size_t)m + 1].tex;   // BuildMaterial m + 1
            for (Model& shaped : materialModels_[(size_t)k][(size_t)m]) shaped.surface = tex;
        }
    }
    for (size_t m = 0; m < stairSteps_.size(); ++m) stairSteps_[m].surface = surfaces_[m].tex;
}
