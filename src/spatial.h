#pragma once
#include <cmath>
#include <vector>
#include "components.h"
#include "ecs.h"

inline Entity spawnGroundItem(World& w, Transform at, int item, int count) {
    const Entity e = w.create();
    w.add(e, at);
    w.add(e, GroundItem{ item, count });
    return e;
}

inline void dropStacks(World& w, Transform at, const std::vector<ItemStack>& stacks) {
    for (const ItemStack& st : stacks) spawnGroundItem(w, at, st.item, st.count);
}

template <typename Slots>
inline void appendStacks(std::vector<ItemStack>& out, const Slots& slots) {
    for (const ItemStack& st : slots) {
        if (st.item > 0 && st.count > 0) out.push_back(st);
    }
}

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

inline std::vector<ItemStack> propHoldings(World& w, Entity prop) {
    std::vector<ItemStack> out;
    if (const Storage* st = w.get<Storage>(prop)) appendStacks(out, st->slots);
    if (const Output* put = w.get<Output>(prop)) appendStacks(out, put->slots);
    if (const BonesPile* pile = w.get<BonesPile>(prop)) appendStacks(out, pile->items);
    return out;
}

inline float flatGap(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}
