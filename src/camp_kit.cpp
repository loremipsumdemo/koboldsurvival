// The starting kit a faction's camp is given (camp_kit.h)
#include "camp_kit.h"
#include <cmath>
#include "build.h"
#include "claims.h"
#include "props.h"

namespace {

// The camp kit's pieces, offsets in tiles from the camp centre (which is snapped to a tile middle). The Altar stands on
// the centre; the campfire two tiles west; four bedrolls around
// the campfire; the barrel two tiles south; two trees just past the bedrolls, for the camp's wood, and three mushrooms
// by them, for its food. Every piece is on its own tile, so none shares ground with another.
struct KitSpot { PropKind kind; float dx, dz; };
const KitSpot CAMP_KIT[] = {
    { PropKind::Altar, 0.0f, 0.0f },
    { PropKind::Campfire, -2.0f, 0.0f },
    { PropKind::Bedroll, -2.0f, -1.0f },
    { PropKind::Bedroll, -2.0f, 1.0f },
    { PropKind::Bedroll, -3.0f, 0.0f },
    { PropKind::Bedroll, -1.0f, 0.0f },
    { PropKind::Barrel, 0.0f, 2.0f },
    { PropKind::Tree, -5.0f, -2.0f },
    { PropKind::Tree, -5.0f, 2.0f },
    { PropKind::Mushroom, -5.0f, 0.0f },
    { PropKind::Mushroom, -6.0f, -1.0f },
    { PropKind::Mushroom, -6.0f, 1.0f },
};

}  // namespace

bool campKitFits(World& w, const Terrain& t, float cx, float cz) {
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    // Every piece's tile must be walkable and free of anything. A free tile has no prop or built prop standing on it.
    const std::vector<Entity> props = w.view<Prop, Transform>();
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        if (!t.walkable((int)std::floor(x), (int)std::floor(z))) return false;
        for (Entity e : props) {
            const Transform* tr = w.get<Transform>(e);
            if (std::fabs(tr->x - x) < 0.5f && std::fabs(tr->z - z) < 0.5f) return false;
        }
    }
    return true;
}

CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, int* serial) {
    CampKit kit;
    // A camp that can't hold the whole kit gets none of it
    if (!campKitFits(w, t, cx, cz)) return kit;
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    kit.x = ox;
    kit.z = oz;
    // Each piece gets the next camp serial, so a save can name it (its storage, lock, fuel, who sleeps on it)
    auto stamp = [&](Entity e) {
        if (serial) w.get<Prop>(e)->serial = (*serial)++;
    };
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        Entity e = NULL_ENTITY;
        if (propInfo(k.kind).built) {
            e = placeBuiltProp(w, t, k.kind, x, z, yaw, faction);
        } else {
            // A level kind (the campfire, bedrolls, barrel, trees, mushrooms): placed as the level places them. All but
            // the trees and mushrooms are owned by the camp; those are nobody's.
            e = w.create();
            w.add(e, Transform{ x, t.heightAt(x, z), z });
            w.add(e, Prop{ k.kind, -1, yaw });
            if (k.kind != PropKind::Tree && k.kind != PropKind::Mushroom) w.add(e, Owner{ faction });
            if (hasStorage(propInfo(k.kind))) w.add(e, Storage{});   // the camp's barrel starts empty
            if (propInfo(k.kind).use == PropUse::PassiveCrafting) {   // the campfire: cold, with an empty output
                w.add(e, Output{});
                w.add(e, Fuel{});
                w.add(e, PropWork{});
            }
            claimOwnership(w, e);   // it stands in the camp's own claim, so the trees are the camp's too
        }
        stamp(e);
        if (k.kind == PropKind::Altar) {
            kit.altar = e;
            const PropInfo& altar = propInfo(PropKind::Altar);
            const int logs = (int)std::ceil(CAMP_UPKEEP_DAYS * 24.0 / altar.fuelHours);
            w.get<Storage>(e)->slots[0] = ItemStack{ altar.input, logs };
        }
        if (k.kind == PropKind::Tree) kit.trees.push_back(e);
    }
    kit.faction = faction;
    kit.yaw = yaw;
    kit.placed = true;
    return kit;
}
