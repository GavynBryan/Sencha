#include <save/HeldRecordSaves.h>

#include <core/logging/LoggingProvider.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/KeptWithZone.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    struct HeldRecordSaves
    {
    };

    JsonValue SavedComponents(const World& world, EntityId record, PersistentEntityId id,
                              const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
    {
        std::vector<std::string> failed;
        JsonValue components =
            WriteEntityRecord(world, record, serializers, context, { .Declared = { .Authored = true, .Saved = true } },
                              SceneRecordPurpose::Save, &failed);
        for (const std::string& key : failed)
            context.Logging->GetLogger<HeldRecordSaves>().Error(
                "save: held record {} left out its '{}', which could not be written", PersistentEntityIdToString(id),
                key);
        return components;
    }

    EntityId LoadRecord(World& world, PersistentEntityId id, const JsonValue& components,
                        const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
    {
        Logger& log = context.Logging->GetLogger<HeldRecordSaves>();
        const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
        if (identities != nullptr && world.IsAlive(identities->TryResolve(id)))
        {
            log.Warn("save: held record {} was not restored; a live entity already has its id",
                     PersistentEntityIdToString(id));
            return {};
        }
        const EntityId record = world.CreateEntity(PersistentStoragePartition);
        world.AddComponent<PersistentIdComponent>(record, PersistentIdComponent{ id });
        if (std::string failed; !ReadEntityRecord(world, record, components, serializers, context, &failed))
        {
            log.Warn("save: held record {} was not restored; this build cannot read its '{}'",
                     PersistentEntityIdToString(id), failed);
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

    std::vector<ZoneListEntity> SavedRecords(const World& world, const HeldRecordIndex& index,
                                             std::vector<EntityId> records,
                                             const ComponentSerializerRegistry& serializers,
                                             SceneSerializationContext& context)
    {
        std::vector<ZoneListEntity> saved;
        std::unordered_set<EntityId, EntityIdHash> seen;
        for (int depth = 0; depth < kHeldDeepestNesting && !records.empty(); ++depth)
        {
            std::vector<EntityId> next;
            for (const EntityId record : records)
            {
                if (!seen.insert(record).second)
                    continue;
                if (!HasAuthoredPersistentId(world, record))
                {
                    const PersistentIdComponent* identity = world.IsRegistered<PersistentIdComponent>()
                        ? world.TryGet<PersistentIdComponent>(record)
                        : nullptr;
                    if (identity != nullptr)
                        saved.push_back(ZoneListEntity{
                            identity->Id, SavedComponents(world, record, identity->Id, serializers, context) });
                    else
                        context.Logging->GetLogger<HeldRecordSaves>().Error(
                            "save: held record {}:{} has no persistent id after settling, so it is not saved",
                            record.Index, record.Generation);
                }
                next.insert(next.end(), index.RecordsOf(record).begin(), index.RecordsOf(record).end());
            }
            records = std::move(next);
        }
        return saved;
    }

    std::vector<EntityId> HeldBy(const HeldRecordIndex& index, EntityId holder)
    {
        return std::vector<EntityId>(index.RecordsOf(holder).begin(), index.RecordsOf(holder).end());
    }

    std::vector<EntityId> Waiting(const HeldRecordIndex& index)
    {
        std::vector<EntityId> records = index.AwaitingRecords();
        const std::vector<EntityId> aside = index.SetAsideRecords();
        records.insert(records.end(), aside.begin(), aside.end());
        return records;
    }

    struct ScopeOrder
    {
        bool operator()(ZoneStateScope a, ZoneStateScope b) const
        {
            return a.Level != b.Level ? a.Level < b.Level : a.Zone < b.Zone;
        }
    };
}

JsonValue CaptureHeldRecords(const World& world, std::span<const EntityId> roots,
                             const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
{
    JsonValue::Array saved;
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !world.IsRegistered<Held>())
        return JsonValue(JsonValue::Object{ { "records", JsonValue(std::move(saved)) } });
    std::vector<EntityId> records;
    for (const EntityId record : index->AwaitingRecords())
        if (!index->AwaitingScopeOf(record).IsValid())
            records.push_back(record);
    const auto take = [&](EntityId holder) {
        const std::vector<EntityId> held = HeldBy(*index, holder);
        records.insert(records.end(), held.begin(), held.end());
    };
    take(EntityId{});
    for (const EntityId root : roots)
        if (world.IsAlive(root))
            take(root);
    for (ZoneListEntity& record : SavedRecords(world, *index, std::move(records), serializers, context))
        saved.push_back(JsonValue(JsonValue::Object{ { "id", JsonValue(PersistentEntityIdToString(record.Id)) },
                                                     { "components", std::move(record.Value) } }));
    return JsonValue(JsonValue::Object{ { "records", JsonValue(std::move(saved)) } });
}

void RestoreHeldRecords(World& world, const JsonValue& data, const ComponentSerializerRegistry& serializers,
                        SceneSerializationContext& context)
{
    DestroyHeldRecords(world);
    const JsonValue* records = data.Find("records");
    if (records == nullptr || !records->IsArray())
        return;
    for (const JsonValue& entry : records->AsArray())
    {
        const JsonValue* id = entry.Find("id");
        const JsonValue* components = entry.Find("components");
        const std::optional<PersistentEntityId> parsed =
            id != nullptr && id->IsString() ? PersistentEntityIdFromString(id->AsString()) : std::nullopt;
        if (parsed.has_value() && parsed->IsValid() && components != nullptr && components->IsObject())
            (void)LoadRecord(world, *parsed, *components, serializers, context);
    }
}

std::vector<ScopedHeldRecords> CaptureLevelHeldRecords(const World& world,
                                                      const std::function<ZoneStateScope(EntityId)>& scopeOf,
                                                      const ComponentSerializerRegistry& serializers,
                                                      SceneSerializationContext& context)
{
    std::vector<ScopedHeldRecords> scoped;
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !world.IsRegistered<Held>())
        return scoped;
    const ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    std::map<ZoneStateScope, std::vector<EntityId>, ScopeOrder> roots;
    for (const EntityId holder : index->Holders())
        if (HasAuthoredPersistentId(world, holder)
            || (IsKeptWithZone(world, holder) && ForgettingOfKept(world, holder) == ZoneStateForgetting::Never))
            if (const ZoneStateScope scope = scopeOf(holder); scope.IsValid())
            {
                const std::vector<EntityId> held = HeldBy(*index, holder);
                roots[scope].insert(roots[scope].end(), held.begin(), held.end());
            }
    for (const EntityId record : Waiting(*index))
        if (const ZoneStateScope scope = index->AwaitingScopeOf(record); scope.IsValid())
        {
            const PersistentEntityId holder = index->AwaitedHolderOf(record);
            if (IsAuthoredPersistentEntityId(holder) || store == nullptr
                || store->ForgettingOf(scope, holder) == ZoneStateForgetting::Never)
                roots[scope].push_back(record);
        }
    for (auto& [scope, records] : roots)
    {
        std::vector<ZoneListEntity> saved = SavedRecords(world, *index, std::move(records), serializers, context);
        if (!saved.empty())
            scoped.push_back(ScopedHeldRecords{ scope, std::move(saved) });
    }
    return scoped;
}

void RestoreLevelHeldRecords(World& world, ZoneStateScope scope, std::span<const ZoneListEntity> records,
                             const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    for (const ZoneListEntity& saved : records)
    {
        const EntityId record = LoadRecord(world, saved.Id, saved.Value, serializers, context);
        const Held* held = record.IsValid() ? world.TryGet<Held>(record) : nullptr;
        if (index != nullptr && held != nullptr && index->AwaitedHolderOf(record).IsValid())
            index->Await(record, held->Holder.Target, scope);
    }
}
