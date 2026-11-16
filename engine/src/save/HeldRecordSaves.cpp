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
#include <world/scene/SceneInstance.h>
#include <world/serialization/ComponentSerializerRegistry.h>
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

    [[nodiscard]] bool SavedBeside(ComponentTypeId type)
    {
        return type == ResolveComponentTypeId<PersistentIdComponent>() || type == ResolveComponentTypeId<SceneInstance>();
    }

    void NameHolder(World& world, EntityId record, EntityId holder)
    {
        Held* held = world.TryGet<Held>(record);
        if (held != nullptr && holder.IsValid() && !held->Holder.Target.IsValid())
            held->Holder = PersistableRef{ .Entity = holder, .Target = EnsurePersistentId(world, holder) };
    }

    JsonValue SaveRecord(World& world, EntityId record, PersistentEntityId id,
                         const ComponentSerializerRegistry& serializers, SceneSerializationContext& context)
    {
        JsonValue::Object components;
        for (const auto& serializer : serializers.Entries())
        {
            if (SavedBeside(serializer->TypeId()))
                continue;
            JsonWriteArchive archive;
            if (!serializer->Save(archive, record, world, context) || !archive.Ok())
                continue;
            if (JsonValue value = archive.TakeValue(); !value.IsNull())
                components.emplace_back(std::string(serializer->JsonKey()), std::move(value));
        }
        return JsonValue(JsonValue::Object{ { "id", JsonValue(PersistentEntityIdToString(id)) },
                                            { "components", JsonValue(std::move(components)) } });
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

JsonValue CaptureHeldRecords(World& world, std::span<const EntityId> roots,
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
    std::unordered_set<EntityId, EntityIdHash> seen;
    for (int depth = 0; depth < kHeldDeepestNesting && !holders.empty(); ++depth)
    {
        std::vector<EntityId> next;
        for (const EntityId holder : holders)
        {
            const std::span<const EntityId> listed = index->RecordsOf(holder);
            const std::vector<EntityId> records(listed.begin(), listed.end());
            for (const EntityId record : records)
            {
                const PersistentEntityId id = EnsurePersistentId(world, record);
                if (!id.IsValid() || !seen.insert(record).second)
                    continue;
                NameHolder(world, record, holder);
                saved.push_back(SaveRecord(world, record, id, serializers, context));
                next.push_back(record);
            }
        }
        holders = std::move(next);
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
