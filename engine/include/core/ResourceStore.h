#pragma once

#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// Type-indexed owner for state attached to one runtime Registry.
// Registration order defines dependency order: later resources are destroyed first.
class ResourceStore
{
public:
    ResourceStore() = default;
    ~ResourceStore() { Clear(); }

    ResourceStore(const ResourceStore&) = delete;
    ResourceStore& operator=(const ResourceStore&) = delete;

    ResourceStore(ResourceStore&& other) noexcept
        : Lookup(std::move(other.Lookup))
        , Ownership(std::move(other.Ownership))
    {
        other.Lookup.clear();
        other.Ownership.clear();
    }

    ResourceStore& operator=(ResourceStore&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            Lookup = std::move(other.Lookup);
            Ownership = std::move(other.Ownership);
            other.Lookup.clear();
            other.Ownership.clear();
        }
        return *this;
    }

    template <typename T, typename... Args>
    T& Register(Args&&... args)
    {
        const std::type_index type(typeid(T));
        auto existing = Lookup.find(type);
        assert(existing == Lookup.end()
               && "ResourceStore: duplicate resource registration for this type");
        if (existing != Lookup.end())
            return static_cast<Model<T>&>(*existing->second).Value;

        auto model = std::make_unique<Model<T>>(std::forward<Args>(args)...);
        T* value = &model->Value;
        Concept* erased = model.get();
        Ownership.push_back(std::move(model));

        try
        {
            const bool inserted = Lookup.emplace(type, erased).second;
            assert(inserted && "ResourceStore: failed to index registered resource");
        }
        catch (...)
        {
            Ownership.pop_back();
            throw;
        }

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
        return it != Lookup.end() ? &static_cast<Model<T>&>(*it->second).Value : nullptr;
    }

    template <typename T>
    const T* TryGet() const
    {
        auto it = Lookup.find(std::type_index(typeid(T)));
        return it != Lookup.end() ? &static_cast<const Model<T>&>(*it->second).Value : nullptr;
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
            if (*it == nullptr)
                continue;

            Lookup.erase((*it)->Type);
            it->reset();
        }

        Ownership.clear();
        assert(Lookup.empty() && "ResourceStore: lookup outlived owned resources");
        Lookup.clear();
    }

private:
    struct Concept
    {
        explicit Concept(std::type_index type)
            : Type(type)
        {
        }

        virtual ~Concept() = default;

        std::type_index Type;
    };

    template <typename T>
    struct Model final : Concept
    {
        template <typename... Args>
        explicit Model(Args&&... args)
            : Concept(std::type_index(typeid(T)))
            , Value(std::forward<Args>(args)...)
        {
        }

        T Value;
    };

    std::unordered_map<std::type_index, Concept*> Lookup;
    std::vector<std::unique_ptr<Concept>> Ownership;
};
