#include <world/held/HeldRecordForms.h>

#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>

#include <string>
#include <unordered_set>

namespace
{
    struct HeldRecordForms
    {
    };
}

std::vector<EntityId> HeldRecordClosure(const World& world, const HeldRecordIndex& index, std::vector<EntityId> records)
{
    std::vector<EntityId> closure;
    std::unordered_set<EntityId, EntityIdHash> seen;
    for (int depth = 0; depth < kHeldDeepestNesting && !records.empty(); ++depth)
    {
        std::vector<EntityId> next;
        for (const EntityId record : records)
        {
            if (!seen.insert(record).second)
                continue;
            if (!HasAuthoredPersistentId(world, record))
                closure.push_back(record);
            next.insert(next.end(), index.RecordsOf(record).begin(), index.RecordsOf(record).end());
        }
        records = std::move(next);
    }
    return closure;
}

std::vector<ZoneListEntity> WriteHeldRecords(const World& world, std::span<const EntityId> records,
                                             const ComponentSerializerRegistry& serializers,
                                             SceneSerializationContext& context)
{
    Logger& log = context.Logging->GetLogger<HeldRecordForms>();
    std::vector<ZoneListEntity> written;
    std::vector<std::string> failed;
    for (const EntityId record : records)
    {
        const PersistentIdComponent* identity =
            world.IsRegistered<PersistentIdComponent>() ? world.TryGet<PersistentIdComponent>(record) : nullptr;
        if (identity == nullptr || !identity->Id.IsValid())
        {
            log.Error("held record {}:{} has no persistent id after settling, so it is not written", record.Index,
                      record.Generation);
            continue;
        }
        failed.clear();
        JsonValue components =
            WriteEntityRecord(world, record, serializers, context, { .Declared = { .Authored = true, .Saved = true } },
                              SceneRecordPurpose::Save, &failed);
        for (const std::string& key : failed)
            log.Error("held record {} left out its '{}', which could not be written",
                      PersistentEntityIdToString(identity->Id), key);
        written.push_back(ZoneListEntity{ identity->Id, std::move(components) });
    }
    return written;
}

EntityId MakeHeldRecord(World& world, StoragePartitionId partition, const ZoneListEntity& saved,
                        const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
{
    Logger& log = context.Logging->GetLogger<HeldRecordForms>();
    const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
    if (identities != nullptr && world.IsAlive(identities->TryResolve(saved.Id)))
    {
        log.Warn("held record {} was not made; a live entity already has its id", PersistentEntityIdToString(saved.Id));
        return {};
    }
    const EntityId record = world.CreateEntity(partition);
    world.AddComponent<PersistentIdComponent>(record, PersistentIdComponent{ saved.Id });
    if (std::string failed; !ReadEntityRecord(world, record, saved.Value, serializers, context, &failed))
    {
        log.Warn("held record {} was not made; this build cannot read its '{}'", PersistentEntityIdToString(saved.Id),
                 failed);
        world.DestroyEntity(record);
        return {};
    }
    if (!world.HasComponent<Held>(record))
    {
        world.DestroyEntity(record);
        return {};
    }
    return record;
}
