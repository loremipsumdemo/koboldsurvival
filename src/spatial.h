#pragma once
#include <cmath>
#include <vector>
#include "components.h"
#include "ecs.h"

// Puts an item stack on the ground. at is a copy, since creating an entity can move the stored components.
inline Entity spawnGroundItem(World& w, Transform at, int item, int count) {
    const Entity e = w.create();
    w.add(e, at);
    w.add(e, GroundItem{ item, count });
    return e;
}

// Puts stacks on the ground, each as its own pile, at one spot
inline void dropStacks(World& w, Transform at, const std::vector<ItemStack>& stacks) {
    for (const ItemStack& st : stacks) spawnGroundItem(w, at, st.item, st.count);
}

// The stacks in a row of slots, added to out
template <typename Slots>
inline void appendStacks(std::vector<ItemStack>& out, const Slots& slots) {
    for (const ItemStack& st : slots) {
        if (st.item > 0 && st.count > 0) out.push_back(st);
    }
}

// Everything a unit carries and wears
inline std::vector<ItemStack> unitBelongings(World& w, Entity u) {
    std::vector<ItemStack> out;
    if (const Inventory* inv = w.get<Inventory>(u)) appendStacks(out, inv->slots);
    if (const Equipment* worn = w.get<Equipment>(u)) {
        for (int item : worn->items) {
            if (item > 0) out.push_back(ItemStack{ item, 1 });
        }
    }
    return out;
}

// Everything a prop holds: its storage, its output and a Bones prop's pile
inline std::vector<ItemStack> propHoldings(World& w, Entity prop) {
    std::vector<ItemStack> out;
    if (const Storage* st = w.get<Storage>(prop)) appendStacks(out, st->slots);
    if (const Output* put = w.get<Output>(prop)) appendStacks(out, put->slots);
    if (const BonesPile* pile = w.get<BonesPile>(prop)) appendStacks(out, pile->items);
    return out;
}

// The flat distance in tiles between two entities, or a large number if either has no place
inline float flatGap(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}
