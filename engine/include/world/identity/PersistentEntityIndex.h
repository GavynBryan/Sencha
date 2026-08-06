#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <unordered_map>

//=============================================================================
// PersistentEntityIndex
//
// World resource mapping persistent entity identities to live generational
// handles. ComponentTraits<PersistentIdComponent> maintains it as identified
// entities are created and destroyed, so its contents are exactly the live
// identified set — a resolved handle needs no separate liveness check until
// the next structural flush. Worlds without this resource (editor documents)
// skip maintenance entirely.
//
// Two live entities with one id is a content defect the cook rejects; if it
// reaches runtime anyway, the first registration wins and the collision is
// counted for diagnostics.
//=============================================================================
class PersistentEntityIndex
{
public:
    bool Register(PersistentEntityId id, EntityId entity)
    {
        const auto [it, inserted] = Entries_.try_emplace(id, entity);
        if (!inserted)
            ++Collisions_;
        return inserted;
    }

    void Unregister(PersistentEntityId id, EntityId entity)
    {
        const auto it = Entries_.find(id);
        // Match on the handle so unregistering a collision loser cannot evict
        // the winner's entry.
        if (it != Entries_.end() && it->second == entity)
            Entries_.erase(it);
    }

    [[nodiscard]] EntityId TryResolve(PersistentEntityId id) const
    {
        const auto it = Entries_.find(id);
        return it != Entries_.end() ? it->second : EntityId{};
    }

    [[nodiscard]] std::size_t Count() const { return Entries_.size(); }
    [[nodiscard]] std::size_t CollisionCount() const { return Collisions_; }

private:
    std::unordered_map<PersistentEntityId, EntityId> Entries_;
    std::size_t Collisions_ = 0;
};
