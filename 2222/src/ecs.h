#pragma once
#include <cstdint>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

using Entity = std::uint32_t;
constexpr Entity NULL_ENTITY = 0;

class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;
    virtual void remove(Entity e) = 0;
};

template <typename T>
class ComponentStorage : public IComponentStorage {
public:
    T& add(Entity e, T value) { return data_[e] = std::move(value); }
    T* get(Entity e) {
        auto it = data_.find(e);
        return it == data_.end() ? nullptr : &it->second;
    }
    bool has(Entity e) const { return data_.count(e) != 0; }
    void remove(Entity e) override { data_.erase(e); }
    const std::unordered_map<Entity, T>& all() const { return data_; }

private:
    std::unordered_map<Entity, T> data_;
};

class World {
public:
    Entity create() { return next_++; }

    // Removes the entity from every component storage
    void destroy(Entity e) {
        for (auto& [type, storage] : storages_) storage->remove(e);
    }

    template <typename T>
    T& add(Entity e, T value) { return storage<T>().add(e, std::move(value)); }

    template <typename T>
    T* get(Entity e) { return storage<T>().get(e); }

    template <typename T>
    bool has(Entity e) { return storage<T>().has(e); }

    // Removes one component type from an entity, leaving the others
    template <typename T>
    void remove(Entity e) { storage<T>().remove(e); }

    // Every entity that has all of the listed component types
    template <typename First, typename... Rest>
    std::vector<Entity> view() {
        std::vector<Entity> out;
        for (const auto& [e, unused] : storage<First>().all()) {
            (void)unused;
            if ((has<Rest>(e) && ...)) out.push_back(e);
        }
        return out;
    }

private:
    template <typename T>
    ComponentStorage<T>& storage() {
        auto key = std::type_index(typeid(T));
        auto it = storages_.find(key);
        if (it == storages_.end()) {
            it = storages_.emplace(key, std::make_unique<ComponentStorage<T>>()).first;
        }
        return static_cast<ComponentStorage<T>&>(*it->second);
    }

    Entity next_ = 1;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> storages_;
};
