#pragma once

#include <ecs/EntityId.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

// Entity location within archetype storage.
struct EntityLocation
{
    uint32_t ArchetypeId = UINT32_MAX;
    uint32_t ChunkIndex  = UINT32_MAX;
    uint32_t RowIndex    = UINT32_MAX;

    bool IsValid() const { return ArchetypeId != UINT32_MAX; }
};

struct EntityRecord
{
    uint32_t       Generation = 0;
    EntityLocation Location;
    bool           Alive = false;
};

// Owns the generational entity id table.
// Generation lives here only — storage reads EntityIndex, not EntityId.
// Generation checks happen at World API boundaries (TryGet, HasComponent, etc.).
class EntityRegistry
{
public:
    EntityId Create()
    {
        if (!FreeList.empty())
        {
            const EntityIndex idx = FreeList.back();
            FreeList.pop_back();
            Records[idx].Alive = true;
            return EntityId{ idx, Records[idx].Generation };
        }

        const EntityIndex idx = static_cast<EntityIndex>(Records.size());
        Records.push_back(EntityRecord{ 1, EntityLocation{}, true });
        return EntityId{ idx, 1 };
    }

    void Destroy(EntityId entity)
    {
        assert(IsAlive(entity));
        EntityRecord& rec = Records[entity.Index];
        rec.Alive    = false;
        ++rec.Generation;
        rec.Location = EntityLocation{};
        FreeList.push_back(entity.Index);
    }

    bool IsAlive(EntityId entity) const
    {
        if (entity.Index >= Records.size()) return false;
        const EntityRecord& rec = Records[entity.Index];
        return rec.Alive && rec.Generation == entity.Generation;
    }

    EntityLocation GetLocation(EntityId entity) const
    {
        assert(IsAlive(entity));
        return Records[entity.Index].Location;
    }

    void SetLocation(EntityId entity, EntityLocation loc)
    {
        assert(entity.Index < Records.size());
        Records[entity.Index].Location = loc;
    }

    // Set location by raw index — used from World internals after swap-and-pop.
    void SetLocationByIndex(EntityIndex idx, EntityLocation loc)
    {
        assert(idx < Records.size());
        Records[idx].Location = loc;
    }

    EntityLocation GetLocationByIndex(EntityIndex idx) const
    {
        assert(idx < Records.size());
        return Records[idx].Location;
    }

    size_t Count() const
    {
        size_t n = 0;
        for (const auto& r : Records) n += r.Alive ? 1 : 0;
        return n;
    }

private:
    std::vector<EntityRecord> Records;
    std::vector<EntityIndex>  FreeList;
};
