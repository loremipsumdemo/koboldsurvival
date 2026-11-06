#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using Entity = std::uint32_t;
constexpr Entity NULL_ENTITY = 0;

class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;
    virtual void remove(Entity e) = 0;
};

// One component type's store.
// - The components sit in a deque, which never moves an element when others are added or removed, so a pointer
//   from get() stays good until that component itself is removed (as it was with the hash map this replaces). A
//   removed component's slot is reused by the next one added.
// - A paged table, indexed by entity id, says which slot an entity's component is in; finding one is two array reads.
// - A packed list of the entities that have one is what views walk.
template <typename T>
class ComponentStorage : public IComponentStorage {
public:
    T& add(Entity e, T value) {
        if (T* existing = get(e)) return *existing = std::move(value);
        std::uint32_t at;
        if (!free_.empty()) {
            at = free_.back();
            free_.pop_back();
            pool_[at].emplace(std::move(value));
        } else {
            at = (std::uint32_t)pool_.size();
            pool_.emplace_back(std::move(value));
        }
        const size_t p = e >> PAGE_BITS;
        if (p >= pages_.size()) pages_.resize(p + 1);
        if (!pages_[p]) pages_[p] = std::make_unique<Page>();
        Page& page = *pages_[p];
        page.ref[e & PAGE_MASK] = Ref{ at + 1, (std::uint32_t)dense_.size() };
        ++page.used;
        dense_.push_back(e);
        return *pool_[at];
    }
    T* get(Entity e) {
        const Ref* r = ref(e);
        return r ? &*pool_[r->slot - 1] : nullptr;
    }
    bool has(Entity e) const { return ref(e) != nullptr; }
    void remove(Entity e) override {
        Ref* r = ref(e);
        if (!r) return;
        const std::uint32_t at = r->slot - 1, d = r->dense;
        const Entity last = dense_.back();
        dense_[d] = last;
        dense_.pop_back();
        if (last != e) ref(last)->dense = d;
        pool_[at].reset();
        free_.push_back(at);
        const size_t p = e >> PAGE_BITS;
        pages_[p]->ref[e & PAGE_MASK] = Ref{};
        if (--pages_[p]->used == 0) pages_[p].reset();
    }
    const std::vector<Entity>& entities() const { return dense_; }
    size_t size() const { return dense_.size(); }

private:
    static constexpr size_t PAGE_BITS = 8;
    static constexpr size_t PAGE_SIZE = size_t(1) << PAGE_BITS;
    static constexpr size_t PAGE_MASK = PAGE_SIZE - 1;
    struct Ref {
        std::uint32_t slot = 0;
        std::uint32_t dense = 0;
    };
    struct Page {
        Ref ref[PAGE_SIZE] = {};
        size_t used = 0;
    };
    Ref* ref(Entity e) {
        const size_t p = e >> PAGE_BITS;
        if (p >= pages_.size() || !pages_[p]) return nullptr;
        Ref& r = pages_[p]->ref[e & PAGE_MASK];
        return r.slot ? &r : nullptr;
    }
    const Ref* ref(Entity e) const { return const_cast<ComponentStorage*>(this)->ref(e); }

    std::deque<std::optional<T>> pool_;
    std::vector<std::uint32_t> free_;
    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<Entity> dense_;
};

namespace ecs_detail {
inline size_t nextTypeId = 0;
template <typename T>
size_t typeId() {
    static const size_t id = nextTypeId++;
    return id;
}
}

class World {
public:
    Entity create() { return next_++; }

    void destroy(Entity e) {
        for (auto& storage : storages_) {
            if (storage) storage->remove(e);
        }
    }

    template <typename T>
    T& add(Entity e, T value) { return storage<T>().add(e, std::move(value)); }

    template <typename T>
    T* get(Entity e) { return storage<T>().get(e); }

    template <typename T>
    bool has(Entity e) { return storage<T>().has(e); }

    template <typename T>
    void remove(Entity e) { storage<T>().remove(e); }

    template <typename T>
    size_t count() { return storage<T>().size(); }

    // Every entity that has all of the listed component types. A copy, so the loop over it may add and remove
    // components freely. It walks the smallest of the listed stores.
    template <typename First, typename... Rest>
    std::vector<Entity> view() {
        if constexpr (sizeof...(Rest) == 0) {
            return storage<First>().entities();
        } else {
            const std::vector<Entity>* base = &storage<First>().entities();
            ((base = storage<Rest>().size() < base->size() ? &storage<Rest>().entities() : base), ...);
            std::vector<Entity> out;
            out.reserve(base->size());
            for (Entity e : *base) {
                if (has<First>(e) && (has<Rest>(e) && ...)) out.push_back(e);
            }
            return out;
        }
    }

private:
    template <typename T>
    ComponentStorage<T>& storage() {
        const size_t id = ecs_detail::typeId<T>();
        if (id >= storages_.size()) storages_.resize(id + 1);
        if (!storages_[id]) storages_[id] = std::make_unique<ComponentStorage<T>>();
        return static_cast<ComponentStorage<T>&>(*storages_[id]);
    }

    Entity next_ = 1;
    std::vector<std::unique_ptr<IComponentStorage>> storages_;
};
