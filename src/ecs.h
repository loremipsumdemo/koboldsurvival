#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Everything in a World is an object, named by an ObjectId: entities, props, ground items, squads, and so on. What an object
// is comes from its components. An entity is only the living kind: see Entity in components.h.
using ObjectId = std::uint32_t;
constexpr ObjectId NO_OBJECT = 0;

// Every change to a component store takes the next number from one counter shared by all of them, so a number seen once is
// never seen again, in this World or another. Anything kept from an earlier look can tell by it that the store changed.
inline std::uint64_t nextRevision() {
    static std::uint64_t last = 0;
    return ++last;
}

class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;
    virtual void remove(ObjectId e) = 0;
};

// One component type's store.
// - The components sit in a deque, which never moves an element when others are added or removed, so a pointer
//   from get() stays good until that component itself is removed. A removed component's slot is reused by the next
//   one added.
// - A paged table, indexed by object id, says which slot an object's component is in; finding one is two array reads.
// - A packed list of the objects that have one is what views walk.
template <typename T>
class ComponentStorage : public IComponentStorage {
public:
    T& add(ObjectId e, T value) {
        revision_ = nextRevision();
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
    T* get(ObjectId e) {
        const Ref* r = ref(e);
        return r ? &*pool_[r->slot - 1] : nullptr;
    }
    bool has(ObjectId e) const { return ref(e) != nullptr; }
    void remove(ObjectId e) override {
        Ref* r = ref(e);
        if (!r) return;
        revision_ = nextRevision();
        const std::uint32_t at = r->slot - 1, d = r->dense;
        const ObjectId last = dense_.back();
        dense_[d] = last;
        dense_.pop_back();
        if (last != e) ref(last)->dense = d;
        pool_[at].reset();
        free_.push_back(at);
        const size_t p = e >> PAGE_BITS;
        pages_[p]->ref[e & PAGE_MASK] = Ref{};
        if (--pages_[p]->used == 0) pages_[p].reset();
    }
    const std::vector<ObjectId>& objects() const { return dense_; }
    size_t size() const { return dense_.size(); }
    // Changed when a component is added or removed here (a component changed in place does not count)
    std::uint64_t revision() const { return revision_; }

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
    Ref* ref(ObjectId e) {
        const size_t p = e >> PAGE_BITS;
        if (p >= pages_.size() || !pages_[p]) return nullptr;
        Ref& r = pages_[p]->ref[e & PAGE_MASK];
        return r.slot ? &r : nullptr;
    }
    const Ref* ref(ObjectId e) const { return const_cast<ComponentStorage*>(this)->ref(e); }

    std::deque<std::optional<T>> pool_;
    std::vector<std::uint32_t> free_;
    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<ObjectId> dense_;
    std::uint64_t revision_ = 0;
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
    ObjectId create() { return next_++; }

    void destroy(ObjectId e) {
        if (e == NO_OBJECT) return;
        for (auto& storage : storages_) {
            if (storage) storage->remove(e);
        }
    }

    template <typename T>
    T& add(ObjectId e, T value) { return storage<T>().add(e, std::move(value)); }

    template <typename T>
    T* get(ObjectId e) { return storage<T>().get(e); }

    template <typename T>
    bool has(ObjectId e) { return storage<T>().has(e); }

    template <typename T>
    void remove(ObjectId e) { storage<T>().remove(e); }

    // When components of type T were last added or removed, in this World or any other (see nextRevision)
    template <typename T>
    std::uint64_t revision() { return storage<T>().revision(); }

    template <typename T>
    size_t count() { return storage<T>().size(); }

    // The one T that belongs to this world as a whole, made the first time it is asked for. It goes with the world, and
    // no view finds it: it is kept under NO_OBJECT, which no view is ever given.
    template <typename T>
    T& resource() {
        ComponentStorage<T>& s = storage<T>();
        if (T* existing = s.get(NO_OBJECT)) return *existing;
        return s.add(NO_OBJECT, T{});
    }

    // Every object that has all of the listed component types. A copy, so the loop over it may add and remove
    // components freely. It walks the smallest of the listed stores.
    template <typename First, typename... Rest>
    std::vector<ObjectId> view() {
        if constexpr (sizeof...(Rest) == 0) {
            return storage<First>().objects();
        } else {
            const std::vector<ObjectId>* base = &storage<First>().objects();
            ((base = storage<Rest>().size() < base->size() ? &storage<Rest>().objects() : base), ...);
            std::vector<ObjectId> out;
            out.reserve(base->size());
            for (ObjectId e : *base) {
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

    ObjectId next_ = 1;
    std::vector<std::unique_ptr<IComponentStorage>> storages_;
};
