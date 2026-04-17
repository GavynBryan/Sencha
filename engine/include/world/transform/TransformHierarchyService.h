#pragma once

#include <entity/EntityHandle.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

//=============================================================================
// TransformHierarchyService
//
// Owns spatial parent/child relationships for entities with transform
// components. The hierarchy is keyed by EntityHandle rather than transform-slot
// keys; transform storage is now an implementation detail of TransformStore.
//=============================================================================
class TransformHierarchyService
{
public:
    // -- Relationship mutation -----------------------------------------------

    void SetParent(EntityHandle child, EntityHandle parent);
    void ClearParent(EntityHandle child);

    void Register(EntityHandle entity);
    void Unregister(EntityHandle entity);

    // -- Queries --------------------------------------------------------------

    EntityHandle GetParent(EntityHandle child) const;

    bool HasParent(EntityHandle child) const;

    const std::vector<EntityHandle>& GetChildren(EntityHandle parent) const;

    bool HasChildren(EntityHandle parent) const;

    std::vector<EntityHandle> GetRoots() const;

    bool IsRegistered(EntityHandle entity) const;

    size_t Count() const;

    uint64_t GetVersion() const { return VersionCounter; }

private:
    static bool SameEntity(EntityHandle a, EntityHandle b)
    {
        return a.Id == b.Id && a.Generation == b.Generation;
    }

    void EnsureRegistered(EntityHandle entity);
    bool WouldCreateCycle(EntityHandle child, EntityHandle parent) const;

    std::unordered_map<EntityId, EntityHandle> Registered;
    std::unordered_map<EntityId, EntityHandle> ChildToParent;
    std::unordered_map<EntityId, std::vector<EntityHandle>> ParentToChildren;
    uint64_t VersionCounter = 0;
};

inline void TransformHierarchyService::SetParent(EntityHandle child, EntityHandle parent)
{
    assert(child.IsValid() && "Cannot parent a null entity.");
    assert(parent.IsValid() && "Cannot parent under a null entity.");
    assert(child.Id != parent.Id && "Cannot parent to self.");
    assert(!WouldCreateCycle(child, parent) && "Cannot create a transform hierarchy cycle.");

    ClearParent(child);

    ChildToParent[child.Id] = parent;
    ParentToChildren[parent.Id].push_back(child);
    EnsureRegistered(child);
    EnsureRegistered(parent);
    ++VersionCounter;
}

inline void TransformHierarchyService::ClearParent(EntityHandle child)
{
    auto it = ChildToParent.find(child.Id);
    if (it == ChildToParent.end())
        return;

    const EntityHandle parent = it->second;
    ChildToParent.erase(it);

    auto pit = ParentToChildren.find(parent.Id);
    if (pit != ParentToChildren.end())
    {
        auto& children = pit->second;
        children.erase(
            std::remove_if(children.begin(), children.end(), [&](EntityHandle candidate)
            {
                return candidate.Id == child.Id;
            }),
            children.end());

        if (children.empty())
            ParentToChildren.erase(pit);
    }

    ++VersionCounter;
}

inline void TransformHierarchyService::Register(EntityHandle entity)
{
    assert(entity.IsValid() && "Cannot register a null entity.");
    EnsureRegistered(entity);
}

inline void TransformHierarchyService::Unregister(EntityHandle entity)
{
    if (!entity.IsValid())
        return;

    ClearParent(entity);

    auto childrenIt = ParentToChildren.find(entity.Id);
    if (childrenIt != ParentToChildren.end())
    {
        for (EntityHandle child : childrenIt->second)
            ChildToParent.erase(child.Id);
        ParentToChildren.erase(childrenIt);
    }

    if (Registered.erase(entity.Id) != 0)
        ++VersionCounter;
}

inline EntityHandle TransformHierarchyService::GetParent(EntityHandle child) const
{
    auto it = ChildToParent.find(child.Id);
    return it == ChildToParent.end() ? EntityHandle{} : it->second;
}

inline bool TransformHierarchyService::HasParent(EntityHandle child) const
{
    return ChildToParent.contains(child.Id);
}

inline const std::vector<EntityHandle>& TransformHierarchyService::GetChildren(EntityHandle parent) const
{
    static const std::vector<EntityHandle> Empty;
    auto it = ParentToChildren.find(parent.Id);
    return it == ParentToChildren.end() ? Empty : it->second;
}

inline bool TransformHierarchyService::HasChildren(EntityHandle parent) const
{
    auto it = ParentToChildren.find(parent.Id);
    return it != ParentToChildren.end() && !it->second.empty();
}

inline std::vector<EntityHandle> TransformHierarchyService::GetRoots() const
{
    std::vector<EntityHandle> roots;
    roots.reserve(Registered.size());

    for (const auto& [id, entity] : Registered)
    {
        if (!ChildToParent.contains(id))
            roots.push_back(entity);
    }

    return roots;
}

inline bool TransformHierarchyService::IsRegistered(EntityHandle entity) const
{
    auto it = Registered.find(entity.Id);
    return it != Registered.end() && SameEntity(it->second, entity);
}

inline size_t TransformHierarchyService::Count() const
{
    return Registered.size();
}

inline void TransformHierarchyService::EnsureRegistered(EntityHandle entity)
{
    auto [it, inserted] = Registered.emplace(entity.Id, entity);
    if (!inserted && !SameEntity(it->second, entity))
    {
        it->second = entity;
        ++VersionCounter;
    }

    if (inserted)
        ++VersionCounter;
}

inline bool TransformHierarchyService::WouldCreateCycle(EntityHandle child, EntityHandle parent) const
{
    EntityHandle cursor = parent;
    while (cursor.IsValid())
    {
        if (cursor.Id == child.Id)
            return true;

        auto it = ChildToParent.find(cursor.Id);
        if (it == ChildToParent.end())
            return false;

        cursor = it->second;
    }

    return false;
}
