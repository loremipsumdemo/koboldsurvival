#include "camp_kit.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "claims.h"
#include "props.h"
#include "inventory.h"
#include "items.h"

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
    { PropKind::WoodStockpile, 2.0f, 0.0f },
    { PropKind::StoneStockpile, 2.0f, 2.0f },
    { PropKind::IronStockpile, 2.0f, -2.0f },
    { PropKind::OreStockpile, 4.0f, 0.0f },
    { PropKind::Stool, -4.0f, -1.0f },
    { PropKind::Stool, -4.0f, 1.0f },
    { PropKind::Stool, -1.0f, -2.0f },
    { PropKind::GameTable, 1.0f, -4.0f },
    { PropKind::Boulder, -3.0f, -4.0f },
    { PropKind::Ore, -3.0f, 4.0f },
    { PropKind::Tree, -5.0f, 4.0f },
    { PropKind::Tree, -5.0f, -2.0f },
    { PropKind::Tree, -5.0f, 2.0f },
    { PropKind::Mushroom, -5.0f, 0.0f },
    { PropKind::Mushroom, -6.0f, -1.0f },
    { PropKind::Mushroom, -6.0f, 1.0f },
};

constexpr int KIT_STOCKPILE_COUNT = 500;
// The item a camp's stockpile of this kind starts full of, or 0
int kitStock(PropKind kind) {
    switch (kind) {
    case PropKind::WoodStockpile: return itemTagged(ITEM_TAG_WOOD_LOG);
    case PropKind::StoneStockpile: return itemTagged(ITEM_TAG_STONE);
    case PropKind::IronStockpile: return itemTagged(ITEM_TAG_IRON_INGOT);
    case PropKind::OreStockpile: return itemTagged(ITEM_TAG_IRON_ORE);
    default: return 0;
    }
}
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
            if (propInfo(k.kind).use != PropUse::Resource) w.add(e, Owner{ faction });
            claimOwnership(w, e);
        }
        if (k.kind == PropKind::Altar) {
            kit.altar = e;
            const PropInfo& altar = propInfo(PropKind::Altar);
            const int bones = (int)std::ceil(CAMP_UPKEEP_DAYS * 24.0 / altar.fuelHours);
            w.get<Storage>(e)->slots[0] = ItemStack{ itemTagged(ITEM_TAG_COMMON_BONES), bones };
        }
        if (const int stock = kitStock(k.kind); stock > 0) {
            // A stockpile starts with 300 of what it holds
            Storage* st = w.get<Storage>(e);
            int left = KIT_STOCKPILE_COUNT;
            for (size_t i = 0; st && i < st->slots.size() && left > 0; ++i) {
                const int n = std::min(left, stackLimit(stock));
                st->slots[i] = ItemStack{ stock, n };
                left -= n;
            }
        }
        if (k.kind == PropKind::Tree) kit.trees.push_back(e);
    }
    kit.faction = faction;
    kit.yaw = yaw;
    kit.placed = true;
    return kit;
}
