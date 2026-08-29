#include <world/serialization/SceneSerializer.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <core/serialization/Serialize.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneFormat.h>
#include <math/MathSchemas.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    void SetError(SceneLoadError* error, std::string message)
    {
        if (error)
            error->Message = std::move(message);
    }

    IComponentSerializer* FindByJsonKey(const ComponentSerializerRegistry& serializers,
                                        std::string_view key)
    {
        for (const auto& entry : serializers.Entries())
        {
            if (key == entry->JsonKey())
                return entry.get();
        }
        return nullptr;
    }

    void RegisterSerializedComponentStorage(const ComponentSerializerRegistry& serializers,
                                            Registry& registry)
    {
        for (const auto& entry : serializers.Entries())
            entry->RegisterStorage(registry);
    }

    void SetParentComponent(Registry& registry, EntityId child, EntityId parent)
    {
        if (!registry.Components.IsRegistered<Parent>())
            return;

        if (Parent* existing = registry.Components.TryGet<Parent>(child))
            existing->Entity = parent;
        else
            registry.Components.AddComponent(child, Parent{ parent });
    }

    void RollbackLoadedEntities(const ComponentSerializerRegistry& serializers,
                                Registry& registry,
                                const std::vector<EntityId>& entities)
    {
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
        {
            for (const auto& serializer : serializers.Entries())
                serializer->Remove(*it, registry);
            registry.Components.DestroyEntity(*it);
        }
    }
}

void RegisterEngineSceneSerializers(ComponentSerializerRegistry& serializers)
{
    // Storage-free: this host wants to read and write scenes without a World.
    // It walks the same feature registrars the runtime does, and each component
    // that declares a chunk id gets a serializer -- so an editor's idea of what
    // a scene can contain cannot drift from the runtime's.
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);
}

JsonValue SaveSceneJson(const Registry& registry,
                        const ComponentSerializerRegistry& serializers)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return SaveSceneJson(registry, serializers, context);
}

JsonValue SaveSceneJson(const Registry& registry,
                        const ComponentSerializerRegistry& serializers,
                        SceneSerializationContext& context)
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
        for (const auto& entry : serializers.Entries())
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

    if (registry.Components.IsRegistered<Parent>())
    {
        for (EntityId child : entities)
        {
            const Parent* p = registry.Components.TryGet<Parent>(child);
            if (!p || !p->Entity.IsValid())
                continue;

            auto childIt = entityToJsonIndex.find(child.Index);
            auto parentIt = entityToJsonIndex.find(p->Entity.Index);
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

bool LoadSceneJson(const JsonValue& root,
                   Registry& registry,
                   const ComponentSerializerRegistry& serializers,
                   SceneLoadError* error)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    return LoadSceneJson(root, registry, serializers, context, error);
}

bool LoadSceneJson(const JsonValue& root,
                   Registry& registry,
                   const ComponentSerializerRegistry& serializers,
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

    RegisterSerializedComponentStorage(serializers, registry);

    for (const JsonValue& entityValue : entitiesValue->AsArray())
    {
        if (!entityValue.IsObject())
        {
            RollbackLoadedEntities(serializers, registry, entities);
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
            RollbackLoadedEntities(serializers, registry, entities);
            SetError(error, "Scene JSON components must be an object.");
            return false;
        }

        for (const auto& [key, componentData] : components->AsObject())
        {
            IComponentSerializer* entry = FindByJsonKey(serializers, key);
            if (!entry)
                continue;

            JsonReadArchive archive(componentData);
            if (!entry->Load(archive, entity, registry, context) || !archive.Ok())
            {
                RollbackLoadedEntities(serializers, registry, entities);
                SetError(error, "Failed to load JSON component '" + key + "'.");
                return false;
            }
        }
    }

    const JsonValue* hierarchyValue = root.Find("hierarchy");
    if (hierarchyValue && !hierarchyValue->IsArray())
    {
        RollbackLoadedEntities(serializers, registry, entities);
        SetError(error, "Scene JSON hierarchy must be an array.");
        return false;
    }

    if (hierarchyValue)
    {
        for (const JsonValue& relation : hierarchyValue->AsArray())
        {
            if (!relation.IsObject())
            {
                RollbackLoadedEntities(serializers, registry, entities);
                SetError(error, "Scene JSON hierarchy relation must be an object.");
                return false;
            }

            const JsonValue* child = relation.Find("child");
            const JsonValue* parent = relation.Find("parent");
            if (!child || !parent || !child->IsNumber() || !parent->IsNumber())
            {
                RollbackLoadedEntities(serializers, registry, entities);
                SetError(error, "Scene JSON hierarchy relation is invalid.");
                return false;
            }

            const auto childIndex = static_cast<size_t>(child->AsNumber());
            const auto parentIndex = static_cast<size_t>(parent->AsNumber());
            if (childIndex >= entities.size() || parentIndex >= entities.size())
            {
                RollbackLoadedEntities(serializers, registry, entities);
                SetError(error, "Scene JSON hierarchy references an unknown entity.");
                return false;
            }

            SetParentComponent(registry, entities[childIndex], entities[parentIndex]);
        }
    }

    return true;
}
