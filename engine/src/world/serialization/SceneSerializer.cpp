#include <world/serialization/SceneSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/JsonArchive.h>
#include <core/serialization/Serialize.h>
#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <world/serialization/SceneFormat.h>
#include <world/transform/TransformHierarchyService.h>
#include <math/MathSchemas.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    std::vector<std::unique_ptr<IComponentSerializer>>& SerializerEntries()
    {
        static std::vector<std::unique_ptr<IComponentSerializer>> entries;
        return entries;
    }

    void SetError(SceneSaveError* error, std::string message)
    {
        if (error)
            error->Message = std::move(message);
    }

    void SetError(SceneLoadError* error, std::string message)
    {
        if (error)
            error->Message = std::move(message);
    }

    IComponentSerializer* FindByChunkMutable(std::uint32_t chunkId)
    {
        for (const auto& entry : SerializerEntries())
        {
            if (entry->BinaryChunkId() == chunkId)
                return entry.get();
        }
        return nullptr;
    }

    IComponentSerializer* FindByJsonKey(std::string_view key)
    {
        for (const auto& entry : SerializerEntries())
        {
            if (key == entry->JsonKey())
                return entry.get();
        }
        return nullptr;
    }

    bool SaveRegistryChunk(const std::vector<EntityId>& entities, BinaryWriter& writer)
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, SceneChunk::Registry, SceneVersion))
            return false;

        const auto count = static_cast<std::uint32_t>(entities.size());
        if (!Serialize(writer, count))
            return false;

        for (EntityId entity : entities)
        {
            if (!Serialize(writer, entity.Index)
                || !Serialize(writer, entity.Generation))
            {
                return false;
            }
        }

        return chunk.End(writer);
    }

    void CollectHierarchyPairs(
        const TransformHierarchyService& hierarchy,
        EntityId entity,
        std::vector<std::pair<EntityId, EntityId>>& pairs)
    {
        for (EntityId child : hierarchy.GetChildren(entity))
        {
            pairs.emplace_back(child, entity);
            CollectHierarchyPairs(hierarchy, child, pairs);
        }
    }

    bool SaveHierarchyChunk(const Registry& registry, BinaryWriter& writer)
    {
        const auto* hierarchy = registry.Resources.TryGet<TransformHierarchyService>();
        if (!hierarchy)
            return true;

        std::vector<std::pair<EntityId, EntityId>> pairs;
        for (EntityId root : hierarchy->GetRoots())
            CollectHierarchyPairs(*hierarchy, root, pairs);

        ChunkWriter chunk;
        if (!chunk.Begin(writer, SceneChunk::Hierarchy, SceneVersion))
            return false;

        const auto count = static_cast<std::uint32_t>(pairs.size());
        if (!Serialize(writer, count))
            return false;

        for (const auto& [child, parent] : pairs)
        {
            if (!Serialize(writer, child.Index)
                || !Serialize(writer, parent.Index))
            {
                return false;
            }
        }

        return chunk.End(writer);
    }

    EntityId RemapEntity(
        EntityIndex savedIndex,
        const std::unordered_map<EntityIndex, EntityId>& remap)
    {
        auto it = remap.find(savedIndex);
        return it == remap.end() ? EntityId{} : it->second;
    }

    bool LoadRegistryChunk(BinaryReader& reader,
                           Registry& registry,
                           std::unordered_map<EntityIndex, EntityId>& remap,
                           std::vector<EntityId>& loadedEntities)
    {
        std::uint32_t count = 0;
        if (!Deserialize(reader, count))
            return false;

        remap.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            EntityIndex savedIndex = 0;
            std::uint16_t savedGeneration = 0;
            if (!Deserialize(reader, savedIndex)
                || !Deserialize(reader, savedGeneration))
            {
                return false;
            }

            EntityId runtime = registry.Components.CreateEntity();
            remap[savedIndex] = runtime;
            loadedEntities.push_back(runtime);
        }

        return true;
    }

    bool LoadHierarchyChunk(BinaryReader& reader,
                            std::uint32_t count,
                            Registry& registry,
                            const std::unordered_map<EntityIndex, EntityId>& remap)
    {
        auto& hierarchy = registry.Resources.Ensure<TransformHierarchyService>();

        for (std::uint32_t i = 0; i < count; ++i)
        {
            EntityIndex childIndex = 0;
            EntityIndex parentIndex = 0;
            if (!Deserialize(reader, childIndex)
                || !Deserialize(reader, parentIndex))
            {
                return false;
            }

            EntityId child = RemapEntity(childIndex, remap);
            EntityId parent = RemapEntity(parentIndex, remap);
            if (!child.IsValid() || !parent.IsValid())
                return false;

            hierarchy.SetParent(child, parent);
        }

        return true;
    }

    bool SaveComponentChunkBinary(const IComponentSerializer& serializer,
                                  const std::vector<EntityId>& entities,
                                  const Registry& registry,
                                  BinaryWriter& writer,
                                  SceneSerializationContext& context)
    {
        // Buffer component data first so the entity count (unknown upfront) can be
        // written before the payload, as required by the chunk format.
        std::stringstream payload(std::ios::in | std::ios::out | std::ios::binary);
        BinaryWriter payloadWriter(payload);

        std::uint32_t count = 0;
        for (EntityId entity : entities)
        {
            if (!serializer.HasComponent(entity, registry))
                continue;

            if (!Serialize(payloadWriter, entity.Index))
                return false;

            BinaryWriteArchive archive(payloadWriter);
            if (!serializer.Save(archive, entity, registry, context) || !archive.Ok())
                return false;

            ++count;
        }

        const std::string payloadBytes = payload.str();
        if (!Serialize(writer, count))
            return false;

        if (!payloadBytes.empty()
            && !writer.WriteBytes(payloadBytes.data(), static_cast<std::streamsize>(payloadBytes.size())))
        {
            return false;
        }

        return true;
    }

    bool LoadComponentChunkBinary(IComponentSerializer& serializer,
                                  BinaryReader& reader,
                                  std::uint32_t count,
                                  Registry& registry,
                                  const std::unordered_map<EntityIndex, EntityId>& remap,
                                  SceneSerializationContext& context)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            EntityIndex savedOwner = 0;
            if (!Deserialize(reader, savedOwner))
                return false;

            EntityId owner = RemapEntity(savedOwner, remap);
            if (!owner.IsValid())
                return false;

            BinaryReadArchive archive(reader);
            if (!serializer.Load(archive, owner, registry, context) || !archive.Ok())
                return false;
        }
        return true;
    }

    void RollbackLoadedEntities(Registry& registry, const std::vector<EntityId>& entities)
    {
        if (auto* hierarchy = registry.Resources.TryGet<TransformHierarchyService>())
        {
            for (auto it = entities.rbegin(); it != entities.rend(); ++it)
                hierarchy->Unregister(*it);
        }

        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
        {
            for (const auto& serializer : SerializerEntries())
                serializer->Remove(*it, registry);
            registry.Components.DestroyEntity(*it);
        }
    }
}

void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer)
{
    if (!serializer)
        return;

    auto& entries = SerializerEntries();
    const auto duplicate = std::ranges::find_if(entries, [&](const auto& existing)
    {
        return existing->BinaryChunkId() == serializer->BinaryChunkId()
            || existing->JsonKey() == serializer->JsonKey();
    });

    if (duplicate == entries.end())
        entries.push_back(std::move(serializer));
}

void ClearComponentSerializers()
{
    SerializerEntries().clear();
}

void InitSceneSerializer()
{
    RegisterComponent<LocalTransform>();
    RegisterComponent<CameraComponent>();
    RegisterComponent<StaticMeshComponent>();
}

const std::vector<std::unique_ptr<IComponentSerializer>>& GetComponentSerializerEntries()
{
    return SerializerEntries();
}

bool SaveSceneBinary(const Registry& registry, BinaryWriter& writer, SceneSaveError* error)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return SaveSceneBinary(registry, writer, context, error);
}

bool SaveSceneBinary(const Registry& registry,
                     BinaryWriter& writer,
                     SceneSerializationContext& context,
                     SceneSaveError* error)
{
    const auto entities = registry.Components.GetAliveEntities();

    if (!WriteBinaryHeader(writer, SceneMagic, SceneVersion))
    {
        SetError(error, "Failed to write scene header.");
        return false;
    }

    if (!SaveRegistryChunk(entities, writer))
    {
        SetError(error, "Failed to write entity registry chunk.");
        return false;
    }

    if (!SaveHierarchyChunk(registry, writer))
    {
        SetError(error, "Failed to write transform hierarchy chunk.");
        return false;
    }

    for (const auto& entry : SerializerEntries())
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, entry->BinaryChunkId(), SceneVersion)
            || !SaveComponentChunkBinary(*entry, entities, registry, writer, context)
            || !chunk.End(writer))
        {
            SetError(error, "Failed to write component chunk.");
            return false;
        }
    }

    return true;
}

bool LoadSceneBinary(BinaryReader& reader, Registry& registry, SceneLoadError* error)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return LoadSceneBinary(reader, registry, context, error);
}

bool LoadSceneBinary(BinaryReader& reader,
                     Registry& registry,
                     SceneSerializationContext& context,
                     SceneLoadError* error)
{
    BinaryHeader header;
    if (!ReadBinaryHeader(reader, header)
        || !ValidateBinaryHeader(header, SceneMagic, SceneVersion))
    {
        SetError(error, "Invalid scene header.");
        return false;
    }

    std::unordered_map<EntityIndex, EntityId> remap;
    std::vector<EntityId> loadedEntities;
    bool loadedRegistry = false;

    while (true)
    {
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader))
            break;

        const ChunkHeader& chunkHeader = chunk.GetHeader();
        bool ok = true;

        if (chunkHeader.Id == SceneChunk::Registry)
        {
            ok = LoadRegistryChunk(reader, registry, remap, loadedEntities);
            loadedRegistry = ok;
        }
        else if (chunkHeader.Id == SceneChunk::Hierarchy)
        {
            std::uint32_t count = 0;
            ok = Deserialize(reader, count)
                && LoadHierarchyChunk(reader, count, registry, remap);
        }
        else if (IComponentSerializer* entry = FindByChunkMutable(chunkHeader.Id))
        {
            std::uint32_t count = 0;
            ok = Deserialize(reader, count)
                && LoadComponentChunkBinary(*entry, reader, count, registry, remap, context);
        }

        if (!ok)
        {
            RollbackLoadedEntities(registry, loadedEntities);
            SetError(error, "Failed to read scene chunk.");
            return false;
        }

        if (!chunk.Skip(reader))
        {
            RollbackLoadedEntities(registry, loadedEntities);
            SetError(error, "Failed to skip scene chunk remainder.");
            return false;
        }
    }

    if (!loadedRegistry)
    {
        RollbackLoadedEntities(registry, loadedEntities);
        SetError(error, "Scene is missing entity registry chunk.");
        return false;
    }

    return true;
}

JsonValue SaveSceneJson(const Registry& registry)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return SaveSceneJson(registry, context);
}

JsonValue SaveSceneJson(const Registry& registry, SceneSerializationContext& context)
{
    JsonValue::Array entitiesJson;
    JsonValue::Array hierarchyJson;
    const auto entities = registry.Components.GetAliveEntities();
    std::unordered_map<EntityIndex, std::uint32_t> entityToJsonIndex;
    entityToJsonIndex.reserve(entities.size());

    for (std::uint32_t i = 0; i < entities.size(); ++i)
    {
        EntityId entity = entities[i];
        entityToJsonIndex[entity.Index] = i;

        JsonValue::Object componentsJson;
        for (const auto& entry : SerializerEntries())
        {
            JsonWriteArchive archive;
            if (!entry->Save(archive, entity, registry, context) || !archive.Ok())
                return {};

            JsonValue component = archive.TakeValue();
            if (!component.IsNull())
                componentsJson.emplace_back(std::string(entry->JsonKey()), std::move(component));
        }

        entitiesJson.emplace_back(JsonValue::Object{
            { "components", JsonValue(std::move(componentsJson)) },
        });
    }

    if (const auto* hierarchy = registry.Resources.TryGet<TransformHierarchyService>())
    {
        for (EntityId child : entities)
        {
            EntityId parent = hierarchy->GetParent(child);
            if (!parent.IsValid())
                continue;

            auto childIt = entityToJsonIndex.find(child.Index);
            auto parentIt = entityToJsonIndex.find(parent.Index);
            if (childIt == entityToJsonIndex.end() || parentIt == entityToJsonIndex.end())
                continue;

            hierarchyJson.emplace_back(JsonValue::Object{
                { "child", JsonValue(static_cast<double>(childIt->second)) },
                { "parent", JsonValue(static_cast<double>(parentIt->second)) },
            });
        }
    }

    return JsonValue(JsonValue::Object{
        { "version", JsonValue(static_cast<double>(SceneVersion)) },
        { "entities", JsonValue(std::move(entitiesJson)) },
        { "hierarchy", JsonValue(std::move(hierarchyJson)) },
    });
}

bool LoadSceneJson(const JsonValue& root, Registry& registry, SceneLoadError* error)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return LoadSceneJson(root, registry, context, error);
}

bool LoadSceneJson(const JsonValue& root,
                   Registry& registry,
                   SceneSerializationContext& context,
                   SceneLoadError* error)
{
    if (!root.IsObject())
    {
        SetError(error, "Scene JSON root must be an object.");
        return false;
    }

    const JsonValue* version = root.Find("version");
    const JsonValue* entitiesValue = root.Find("entities");
    if (!version || !version->IsNumber()
        || static_cast<std::uint32_t>(version->AsNumber()) != SceneVersion
        || !entitiesValue || !entitiesValue->IsArray())
    {
        SetError(error, "Scene JSON has an invalid version or entity list.");
        return false;
    }

    std::vector<EntityId> entities;
    entities.reserve(entitiesValue->AsArray().size());

    for (const JsonValue& entityValue : entitiesValue->AsArray())
    {
        if (!entityValue.IsObject())
        {
            RollbackLoadedEntities(registry, entities);
            SetError(error, "Scene JSON entity must be an object.");
            return false;
        }

        EntityId entity = registry.Components.CreateEntity();
        entities.push_back(entity);

        const JsonValue* components = entityValue.Find("components");
        if (!components)
            continue;

        if (!components->IsObject())
        {
            RollbackLoadedEntities(registry, entities);
            SetError(error, "Scene JSON components must be an object.");
            return false;
        }

        for (const auto& [key, componentData] : components->AsObject())
        {
            IComponentSerializer* entry = FindByJsonKey(key);
            if (!entry)
                continue;

            JsonReadArchive archive(componentData);
            if (!entry->Load(archive, entity, registry, context) || !archive.Ok())
            {
                RollbackLoadedEntities(registry, entities);
                SetError(error, "Failed to load JSON component.");
                return false;
            }
        }
    }

    const JsonValue* hierarchyValue = root.Find("hierarchy");
    if (hierarchyValue && !hierarchyValue->IsArray())
    {
        RollbackLoadedEntities(registry, entities);
        SetError(error, "Scene JSON hierarchy must be an array.");
        return false;
    }

    if (hierarchyValue)
    {
        auto& hierarchy = registry.Resources.Ensure<TransformHierarchyService>();
        for (const JsonValue& relation : hierarchyValue->AsArray())
        {
            if (!relation.IsObject())
            {
                RollbackLoadedEntities(registry, entities);
                SetError(error, "Scene JSON hierarchy relation must be an object.");
                return false;
            }

            const JsonValue* child = relation.Find("child");
            const JsonValue* parent = relation.Find("parent");
            if (!child || !parent || !child->IsNumber() || !parent->IsNumber())
            {
                RollbackLoadedEntities(registry, entities);
                SetError(error, "Scene JSON hierarchy relation is invalid.");
                return false;
            }

            const auto childIndex = static_cast<size_t>(child->AsNumber());
            const auto parentIndex = static_cast<size_t>(parent->AsNumber());
            if (childIndex >= entities.size() || parentIndex >= entities.size())
            {
                RollbackLoadedEntities(registry, entities);
                SetError(error, "Scene JSON hierarchy references an unknown entity.");
                return false;
            }

            hierarchy.SetParent(entities[childIndex], entities[parentIndex]);
        }
    }

    return true;
}
