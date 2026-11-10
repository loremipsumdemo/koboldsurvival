#pragma once
#include <cmath>
#include <vector>
#include "components.h"
#include "ecs.h"

// storey: the floor it lies on (0 the ground). Something dropped upstairs must say so, or it is taken for the ground
// floor's and can't be seen picked or clicked on its own floor.
inline ObjectId spawnGroundItem(World& w, Transform at, int item, int count, int storey = 0) {
    const ObjectId e = w.create();
    w.add(e, at);
    w.add(e, GroundItem{ item, count });
    if (storey > 0) w.add(e, Storey{ storey });
    return e;
}

inline void dropStacks(World& w, Transform at, const std::vector<ItemStack>& stacks, int storey = 0) {
    for (const ItemStack& st : stacks) spawnGroundItem(w, at, st.item, st.count, storey);
}

template <typename Slots>
inline void appendStacks(std::vector<ItemStack>& out, const Slots& slots) {
    for (const ItemStack& st : slots) {
        if (st.item > 0 && st.count > 0) out.push_back(st);
    }
}

inline std::vector<ItemStack> unitBelongings(World& w, ObjectId u) {
    std::vector<ItemStack> out;
    if (const Inventory* inv = w.get<Inventory>(u)) appendStacks(out, inv->slots);
    if (const Equipment* worn = w.get<Equipment>(u)) {
        for (int item : worn->items) {
            if (item > 0) out.push_back(ItemStack{ item, 1 });
        }
    }
    return out;
}

inline std::vector<ItemStack> propHoldings(World& w, ObjectId prop) {
    std::vector<ItemStack> out;
    if (const Storage* st = w.get<Storage>(prop)) appendStacks(out, st->slots);
    if (const Output* put = w.get<Output>(prop)) appendStacks(out, put->slots);
    if (const BonesPile* pile = w.get<BonesPile>(prop)) appendStacks(out, pile->items);
    return out;
}

inline float flatGap(World& w, ObjectId a, ObjectId b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    // On different floors of a building, nothing is in reach of the other
    if (storeyOf(w, a) != storeyOf(w, b)) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}
