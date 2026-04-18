#pragma once

#include <entity/EntityId.h>
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
// components. The hierarchy is keyed by EntityId rather than transform-slot
// keys; transform storage is now an implementation detail of TransformStore.
//=============================================================================
class TransformHierarchyService
{
public:
    // -- Relationship mutation -----------------------------------------------

    void SetParent(EntityId child, EntityId parent);
    void ClearParent(EntityId child);

    void Register(EntityId entity);
    void Unregister(EntityId entity);

    // -- Queries --------------------------------------------------------------

    EntityId GetParent(EntityId child) const;

    bool HasParent(EntityId child) const;

    const std::vector<EntityId>& GetChildren(EntityId parent) const;

    bool HasChildren(EntityId parent) const;

    std::vector<EntityId> GetRoots() const;

    bool IsRegistered(EntityId entity) const;

    size_t Count() const;

    uint64_t GetVersion() const { return VersionCounter; }

private:
    static bool SameEntity(EntityId a, EntityId b)
    {
        return a.Index == b.Index && a.Generation == b.Generation;
    }

    void EnsureRegistered(EntityId entity);
    bool WouldCreateCycle(EntityId child, EntityId parent) const;

    std::unordered_map<EntityIndex, EntityId> Registered;
    std::unordered_map<EntityIndex, EntityId> ChildToParent;
    std::unordered_map<EntityIndex, std::vector<EntityId>> ParentToChildren;
    uint64_t VersionCounter = 0;
};

inline void TransformHierarchyService::SetParent(EntityId child, EntityId parent)
{
    assert(child.IsValid() && "Cannot parent a null entity.");
    assert(parent.IsValid() && "Cannot parent under a null entity.");
    assert(child.Index != parent.Index && "Cannot parent to self.");
    assert(!WouldCreateCycle(child, parent) && "Cannot create a transform hierarchy cycle.");

    ClearParent(child);

    ChildToParent[child.Index] = parent;
    ParentToChildren[parent.Index].push_back(child);
    EnsureRegistered(child);
    EnsureRegistered(parent);
    ++VersionCounter;
}

inline void TransformHierarchyService::ClearParent(EntityId child)
{
    auto it = ChildToParent.find(child.Index);
    if (it == ChildToParent.end())
        return;

    const EntityId parent = it->second;
    ChildToParent.erase(it);

    auto pit = ParentToChildren.find(parent.Index);
    if (pit != ParentToChildren.end())
    {
        auto& children = pit->second;
        children.erase(
            std::remove_if(children.begin(), children.end(), [&](EntityId candidate)
            {
                return candidate.Index == child.Index;
            }),
            children.end());

        if (children.empty())
            ParentToChildren.erase(pit);
    }

    ++VersionCounter;
}

inline void TransformHierarchyService::Register(EntityId entity)
{
    assert(entity.IsValid() && "Cannot register a null entity.");
    EnsureRegistered(entity);
}

inline void TransformHierarchyService::Unregister(EntityId entity)
{
    if (!entity.IsValid())
        return;

    ClearParent(entity);

    auto childrenIt = ParentToChildren.find(entity.Index);
    if (childrenIt != ParentToChildren.end())
    {
        for (EntityId child : childrenIt->second)
            ChildToParent.erase(child.Index);
        ParentToChildren.erase(childrenIt);
    }

    if (Registered.erase(entity.Index) != 0)
        ++VersionCounter;
}

inline EntityId TransformHierarchyService::GetParent(EntityId child) const
{
    auto it = ChildToParent.find(child.Index);
    return it == ChildToParent.end() ? EntityId{} : it->second;
}

inline bool TransformHierarchyService::HasParent(EntityId child) const
{
    return ChildToParent.contains(child.Index);
}

inline const std::vector<EntityId>& TransformHierarchyService::GetChildren(EntityId parent) const
{
    static const std::vector<EntityId> Empty;
    auto it = ParentToChildren.find(parent.Index);
    return it == ParentToChildren.end() ? Empty : it->second;
}

inline bool TransformHierarchyService::HasChildren(EntityId parent) const
{
    auto it = ParentToChildren.find(parent.Index);
    return it != ParentToChildren.end() && !it->second.empty();
}

inline std::vector<EntityId> TransformHierarchyService::GetRoots() const
{
    std::vector<EntityId> roots;
    roots.reserve(Registered.size());

    for (const auto& [id, entity] : Registered)
    {
        if (!ChildToParent.contains(id))
            roots.push_back(entity);
    }

    return roots;
}

inline bool TransformHierarchyService::IsRegistered(EntityId entity) const
{
    auto it = Registered.find(entity.Index);
    return it != Registered.end() && SameEntity(it->second, entity);
}

inline size_t TransformHierarchyService::Count() const
{
    return Registered.size();
}

inline void TransformHierarchyService::EnsureRegistered(EntityId entity)
{
    auto [it, inserted] = Registered.emplace(entity.Index, entity);
    if (!inserted && !SameEntity(it->second, entity))
    {
        it->second = entity;
        ++VersionCounter;
    }

    if (inserted)
        ++VersionCounter;
}

inline bool TransformHierarchyService::WouldCreateCycle(EntityId child, EntityId parent) const
{
    EntityId cursor = parent;
    while (cursor.IsValid())
    {
        if (cursor.Index == child.Index)
            return true;

        auto it = ChildToParent.find(cursor.Index);
        if (it == ChildToParent.end())
            return false;

        cursor = it->second;
    }

    return false;
}
