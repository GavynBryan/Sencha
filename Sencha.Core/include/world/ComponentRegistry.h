#pragma once

#include <world/IComponentStore.h>
#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>

//=============================================================================
// ComponentRegistry
//
// Type-indexed owner of component stores. Engine modules and game code both
// register stores here; systems resolve a typed pointer once at init and cache
// it — no map lookup in hot paths.
//
// Usage:
//   // Registration (once, at world/system setup):
//   auto& health = world.Components.Register<HealthStore>();
//
//   // Resolution (once per system, at init):
//   HealthStore* health = world.Components.Get<HealthStore>();
//
//   // Hot path (direct pointer dereference, zero overhead):
//   health->TryGet(entity);
//=============================================================================
class ComponentRegistry
{
public:
    // Construct a store of type T in-place and return a reference to it.
    // Asserts on duplicate registration.
    template <typename T, typename... Args>
    T& Register(Args&&... args)
    {
        static_assert(std::is_base_of_v<IComponentStore, T>,
            "T must derive from IComponentStore to be registered in ComponentRegistry");

        auto [it, inserted] = Stores.emplace(
            std::type_index(typeid(T)),
            std::make_unique<T>(std::forward<Args>(args)...));

        assert(inserted && "ComponentRegistry: duplicate store registration for this type");
        return static_cast<T&>(*it->second);
    }

    // Retrieve a previously registered store. Asserts if not found.
    template <typename T>
    T& Get() const
    {
        auto it = Stores.find(std::type_index(typeid(T)));
        assert(it != Stores.end() && "ComponentRegistry: store not registered");
        return static_cast<T&>(*it->second);
    }

    // Retrieve a store, or nullptr if not registered.
    template <typename T>
    T* TryGet() const
    {
        auto it = Stores.find(std::type_index(typeid(T)));
        return it != Stores.end() ? static_cast<T*>(it->second.get()) : nullptr;
    }

    template <typename T>
    bool Has() const
    {
        return Stores.count(std::type_index(typeid(T))) > 0;
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStore>> Stores;
};
