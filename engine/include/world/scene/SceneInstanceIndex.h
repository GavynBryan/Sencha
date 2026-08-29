#pragma once

#include <ecs/EntityId.h>
#include <world/scene/SceneInstance.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// SceneInstanceIndex
//
// World resource mapping a scene instance's identity to the live entities
// carrying it. ComponentTraits<SceneInstance> maintains it as members are
// created and destroyed -- gameplay destroying one spawned entity prunes the
// group through the component's own remove hook, so the index needs no sweep
// and never goes stale. Worlds without this resource skip maintenance.
//=============================================================================
class SceneInstanceIndex
{
public:
    void Register(SceneInstanceId id, EntityId entity)
    {
        Groups_[id].push_back(entity);
    }

    void Unregister(SceneInstanceId id, EntityId entity)
    {
        const auto it = Groups_.find(id);
        if (it == Groups_.end())
            return;
        std::erase(it->second, entity);
        if (it->second.empty())
            Groups_.erase(it);
    }

    // The instance's live members, in creation order; empty when no entity
    // carries the id (never spawned, or fully destroyed).
    [[nodiscard]] std::span<const EntityId> Entities(SceneInstanceId id) const
    {
        const auto it = Groups_.find(id);
        if (it == Groups_.end())
            return {};
        return { it->second.data(), it->second.size() };
    }

    [[nodiscard]] std::size_t GroupCount() const { return Groups_.size(); }

private:
    std::unordered_map<SceneInstanceId, std::vector<EntityId>> Groups_;
};
