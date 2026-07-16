#pragma once

#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// Type-indexed owner for state attached to one Registry.
// Registration order defines dependency order: later resources are destroyed first.
class ResourceStore
{
public:
    ResourceStore() = default;
    ~ResourceStore() { Clear(); }

    ResourceStore(const ResourceStore&) = delete;
    ResourceStore& operator=(const ResourceStore&) = delete;
    ResourceStore(ResourceStore&&) = delete;
    ResourceStore& operator=(ResourceStore&&) = delete;

    template <typename T, typename... Args>
    T& Register(Args&&... args)
    {
        const std::type_index type(typeid(T));
        auto existing = Lookup.find(type);
        assert(existing == Lookup.end()
               && "ResourceStore: duplicate resource registration for this type");
        if (existing != Lookup.end())
            return *static_cast<T*>(existing->second);

        auto resource = std::make_unique<T>(std::forward<Args>(args)...);
        T* value = resource.get();
        Ownership.push_back(Entry{
            type,
            value,
            [](void* pointer) { delete static_cast<T*>(pointer); },
        });

        try
        {
            auto [it, inserted] = Lookup.emplace(type, value);
            assert(inserted && "ResourceStore: failed to index registered resource");
            if (!inserted)
            {
                Ownership.pop_back();
                return *static_cast<T*>(it->second);
            }
        }
        catch (...)
        {
            Ownership.pop_back();
            throw;
        }

        resource.release();
        return *value;
    }

    template <typename T, typename... Args>
    T& Ensure(Args&&... args)
    {
        if (T* existing = TryGet<T>())
            return *existing;
        return Register<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    T& Get()
    {
        T* value = TryGet<T>();
        assert(value != nullptr && "ResourceStore: resource not registered");
        return *value;
    }

    template <typename T>
    const T& Get() const
    {
        const T* value = TryGet<T>();
        assert(value != nullptr && "ResourceStore: resource not registered");
        return *value;
    }

    template <typename T>
    T* TryGet()
    {
        auto it = Lookup.find(std::type_index(typeid(T)));
        return it != Lookup.end() ? static_cast<T*>(it->second) : nullptr;
    }

    template <typename T>
    const T* TryGet() const
    {
        auto it = Lookup.find(std::type_index(typeid(T)));
        return it != Lookup.end() ? static_cast<const T*>(it->second) : nullptr;
    }

    template <typename T>
    bool Has() const
    {
        return Lookup.count(std::type_index(typeid(T))) != 0;
    }

    void Clear()
    {
        for (auto it = Ownership.rbegin(); it != Ownership.rend(); ++it)
        {
            Lookup.erase(it->Type);
            if (it->Value != nullptr && it->Destroy != nullptr)
                it->Destroy(it->Value);
            it->Value = nullptr;
        }

        Ownership.clear();
        assert(Lookup.empty() && "ResourceStore: lookup outlived owned resources");
        Lookup.clear();
    }

private:
    struct Entry
    {
        std::type_index Type;
        void* Value = nullptr;
        void (*Destroy)(void*) = nullptr;
    };

    std::unordered_map<std::type_index, void*> Lookup;
    std::vector<Entry> Ownership;
};
