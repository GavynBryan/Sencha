#include <save/HeldRecordSaves.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
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

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    struct HeldRecordSaves
    {
    };

    JsonValue SaveRecord(const World& world, EntityId record, PersistentEntityId id,
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
        return JsonValue(JsonValue::Object{ { "id", JsonValue(PersistentEntityIdToString(id)) },
                                            { "components", std::move(components) } });
    }

    void LoadRecord(World& world, PersistentEntityId id, const JsonValue& components,
                    const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
    {
        Logger& log = context.Logging->GetLogger<HeldRecordSaves>();
        const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
        if (identities != nullptr && world.IsAlive(identities->TryResolve(id)))
        {
            log.Warn("save: held record {} was not restored; a live entity already has its id",
                     PersistentEntityIdToString(id));
            return;
        }
        const EntityId record = world.CreateEntity(PersistentStoragePartition);
        world.AddComponent<PersistentIdComponent>(record, PersistentIdComponent{ id });
        for (const auto& [key, value] : components.AsObject())
        {
            IComponentSerializer* serializer = serializers.FindByJsonKey(key);
            JsonReadArchive archive(value);
            if (serializer == nullptr || !serializer->LoadIntoWorld(archive, record, world, context) || !archive.Ok())
            {
                log.Warn("save: held record {} was not restored; this build cannot read its '{}'",
                         PersistentEntityIdToString(id), key);
                world.DestroyEntity(record);
                return;
            }
        }
        if (!world.HasComponent<Held>(record))
            world.DestroyEntity(record);
    }
}

JsonValue CaptureHeldRecords(const World& world, std::span<const EntityId> roots,
                             const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
{
    JsonValue::Array saved;
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !world.IsRegistered<Held>())
        return JsonValue(JsonValue::Object{ { "records", JsonValue(std::move(saved)) } });
    std::vector<EntityId> holders{ EntityId{} };
    for (const EntityId root : roots)
        if (world.IsAlive(root))
            holders.push_back(root);
    for (const EntityId holder : index->Holders())
        if (HasAuthoredPersistentId(world, holder))
            holders.push_back(holder);
    std::vector<EntityId> records = index->AwaitingRecords();
    for (const EntityId holder : holders)
        records.insert(records.end(), index->RecordsOf(holder).begin(), index->RecordsOf(holder).end());
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
                const PersistentIdComponent* identity =
                    world.IsRegistered<PersistentIdComponent>() ? world.TryGet<PersistentIdComponent>(record) : nullptr;
                if (identity != nullptr)
                    saved.push_back(SaveRecord(world, record, identity->Id, serializers, context));
                else
                    context.Logging->GetLogger<HeldRecordSaves>().Error(
                        "save: held record {}:{} has no persistent id after settling, so it is not saved",
                        record.Index, record.Generation);
            }
            next.insert(next.end(), index->RecordsOf(record).begin(), index->RecordsOf(record).end());
        }
        records = std::move(next);
    }
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
            LoadRecord(world, *parsed, *components, serializers, context);
    }
}
