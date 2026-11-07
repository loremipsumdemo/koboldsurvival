#include "camp_kit.h"
#include <cmath>
#include "build.h"
#include "claims.h"
#include "props.h"

namespace {
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
}

bool campKitFits(World& w, const Terrain& t, float cx, float cz) {
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    const std::vector<ObjectId> props = w.view<Prop, Transform>();
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        if (!t.walkable((int)std::floor(x), (int)std::floor(z))) return false;
        for (ObjectId e : props) {
            const Transform* tr = w.get<Transform>(e);
            if (std::fabs(tr->x - x) < 0.5f && std::fabs(tr->z - z) < 0.5f) return false;
        }
    }
    return true;
}

CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw) {
    CampKit kit;
    if (!campKitFits(w, t, cx, cz)) return kit;
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    kit.x = ox;
    kit.z = oz;
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        ObjectId e = NO_OBJECT;
        if (propInfo(k.kind).built) {
            e = placeBuiltProp(w, t, k.kind, x, z, yaw, faction);
        } else {
            e = makeProp(w, k.kind, Transform{ x, t.heightAt(x, z), z }, yaw);
            if (k.kind != PropKind::Tree && k.kind != PropKind::Mushroom) w.add(e, Owner{ faction });
            claimOwnership(w, e);
        }
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
