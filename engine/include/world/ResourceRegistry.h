#pragma once

#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

//=============================================================================
// ResourceRegistry
//
// Type-indexed owner for registry-local resources. Resources are per-registry
// state that are not entity-indexed component stores: hierarchy services,
// active camera state, physics worlds, audio zone state, and similar systems.
//=============================================================================
class ResourceRegistry
{
public:
    template <typename T, typename... Args>
    T& Register(Args&&... args)
    {
        auto [it, inserted] = Resources.emplace(
            std::type_index(typeid(T)),
            std::make_unique<Model<T>>(std::forward<Args>(args)...));

        assert(inserted && "ResourceRegistry: duplicate resource registration for this type");
        return static_cast<Model<T>&>(*it->second).Value;
    }

    template <typename T, typename... Args>
    T& Ensure(Args&&... args)
    {
        const auto type = std::type_index(typeid(T));
        auto it = Resources.find(type);
        if (it != Resources.end())
            return static_cast<Model<T>&>(*it->second).Value;

        auto [insertedIt, inserted] = Resources.emplace(
            type,
            std::make_unique<Model<T>>(std::forward<Args>(args)...));

        assert(inserted && "ResourceRegistry: failed to insert ensured resource");
        return static_cast<Model<T>&>(*insertedIt->second).Value;
    }

    template <typename T>
    T& Get() const
    {
        auto it = Resources.find(std::type_index(typeid(T)));
        assert(it != Resources.end() && "ResourceRegistry: resource not registered");
        return static_cast<Model<T>&>(*it->second).Value;
    }

    template <typename T>
    T* TryGet() const
    {
        auto it = Resources.find(std::type_index(typeid(T)));
        return it != Resources.end() ? &static_cast<Model<T>&>(*it->second).Value : nullptr;
    }

    template <typename T>
    bool Has() const
    {
        return Resources.count(std::type_index(typeid(T))) > 0;
    }

private:
    struct Concept
    {
        virtual ~Concept() = default;
    };

    template <typename T>
    struct Model final : Concept
    {
        template <typename... Args>
        explicit Model(Args&&... args)
            : Value(std::forward<Args>(args)...)
        {
        }

        T Value;
    };

    std::unordered_map<std::type_index, std::unique_ptr<Concept>> Resources;
};
