#include "SceneCookProjection.h"

#include "DocumentSerialization.h"
#include "EditorDocument.h"

#include <core/serialization/JsonArchive.h>
#include <navigation/NavLinkComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

JsonValue BuildRuntimeSceneProjection(const EditorDocument& document,
                                LoggingProvider& logging,
                                AssetSystem* assets,
                                std::span<const CookTriggerSensor> sensors,
                                std::span<const EntityId> referencedEntities,
                                std::unordered_map<EntityId, std::uint32_t, EntityIdHash>& sceneIndices)
{
    SceneSerializationContext context(logging, assets);
    JsonValue serialized =
        SaveSceneJson(document.GetRegistry(), EditorSceneSerializers(), context);
    const JsonValue* sourceEntities = serialized.Find("entities");
    const JsonValue* sourceHierarchy = serialized.Find("hierarchy");
    if (sourceEntities == nullptr || !sourceEntities->IsArray()
        || sourceHierarchy == nullptr || !sourceHierarchy->IsArray())
        return {};

    const std::vector<EntityId> alive =
        document.GetRegistry().Components.GetAliveEntities();
    if (alive.size() != sourceEntities->AsArray().size())
        return {};

    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> sourceIndices;
    std::vector<bool> runtimeEntity(alive.size(), false);
    std::vector<bool> retained(alive.size(), false);
    for (std::uint32_t index = 0; index < alive.size(); ++index)
    {
        sourceIndices.emplace(alive[index], index);
        runtimeEntity[index] = document.GetScene().TryGetBrush(alive[index]) == nullptr;
        if (!runtimeEntity[index] && document.GetScene().IsEntityEffectivelyVisible(alive[index]))
        {
            const auto* components = sourceEntities->AsArray()[index].Find("components");
            if (components != nullptr && components->IsObject())
                runtimeEntity[index] = std::ranges::any_of(components->AsObject(), [](const auto& field)
                {
                    return !IsEditorOnlyComponent(field.first)
                        && field.first != TypeSchema<LocalTransform>::Name
                        && field.first != TypeSchema<PersistentIdComponent>::Name
                        && field.first != TypeSchema<NavLink>::Name;
                });
        }
        retained[index] = runtimeEntity[index];
    }
    for (const EntityId entity : referencedEntities)
        if (const auto found = sourceIndices.find(entity); found != sourceIndices.end())
            retained[found->second] = true;
    for (std::size_t index = 0; index < alive.size(); ++index)
    {
        if (!retained[index])
            continue;
        for (EntityId parent = document.GetScene().GetParent(alive[index]); parent.IsValid();
             parent = document.GetScene().GetParent(parent))
        {
            const auto found = sourceIndices.find(parent);
            if (found == sourceIndices.end() || retained[found->second])
                break;
            retained[found->second] = true;
        }
    }
    std::unordered_map<EntityId, const Collider*, EntityIdHash> sensorShapes;
    for (const auto& sensor : sensors)
        sensorShapes.emplace(sensor.Entity, &sensor.Sensor);
    std::vector<std::uint32_t> remap(alive.size(), UINT32_MAX);
    JsonValue::Array entities;
    for (std::size_t oldIndex = 0; oldIndex < alive.size(); ++oldIndex)
    {
        const EntityId entity = alive[oldIndex];
        if (!retained[oldIndex])
            continue;

        remap[oldIndex] = static_cast<std::uint32_t>(entities.size());
        sceneIndices.emplace(entity, remap[oldIndex]);
        JsonValue entityJson = sourceEntities->AsArray()[oldIndex];
        if (JsonValue* components = entityJson.Find("components");
            components != nullptr && components->IsObject())
        {
            if (!runtimeEntity[oldIndex])
                std::erase_if(components->AsObject(), [](const auto& field)
                {
                    return field.first != TypeSchema<LocalTransform>::Name
                        && field.first != TypeSchema<PersistentIdComponent>::Name;
                });
            if (const auto found = sensorShapes.find(entity); found != sensorShapes.end())
            {
                JsonWriteArchive archive;
                if (!SceneComponentValidation<Collider>::Validate(*found->second, archive)
                    || !SceneComponentSerialization::SaveFields(archive, *found->second, context))
                    return {};
                components->AsObject().emplace_back("collider", archive.TakeValue());
            }
            if (auto* trigger = components->Find("trigger"))
                if (auto* program = trigger->Find("program"))
                    for (const char* field : { "commands", "conditions" })
                        if (auto* rows = program->Find(field); rows != nullptr && rows->IsArray())
                            std::erase_if(rows->AsArray(), [](const JsonValue& row)
                            {
                                const auto* enabled = row.Find("enabled");
                                return enabled != nullptr && enabled->IsBool() && !enabled->AsBool();
                            });
            // Runtime import rejects editor-only component keys.
            std::erase_if(components->AsObject(),
                [](const auto& field) { return IsEditorOnlyComponent(field.first); });

            // Navigation links are published through the navigation output.
            std::erase_if(components->AsObject(),
                [](const auto& field) { return field.first == TypeSchema<NavLink>::Name; });

            // Expanded placements retain their authored instance provenance.
            if (const SceneInstanceId owner = document.SceneInstanceOwnerOf(entity);
                owner.IsValid())
            {
                components->AsObject().push_back({ "scene_instance",
                    JsonValue(JsonValue::Object{
                        { "source", JsonValue(document.SceneInstanceSourceOf(entity)) },
                        { "id", JsonValue(SceneInstanceIdToString(owner)) },
                    }) });
            }
        }
        entities.push_back(std::move(entityJson));
    }

    JsonValue::Array hierarchy;
    for (const JsonValue& relation : sourceHierarchy->AsArray())
    {
        const JsonValue* child = relation.Find("child");
        const JsonValue* parent = relation.Find("parent");
        if (child == nullptr || !child->IsNumber()
            || parent == nullptr || !parent->IsNumber())
            continue;
        const std::uint32_t oldChild = static_cast<std::uint32_t>(child->AsNumber());
        const std::uint32_t oldParent = static_cast<std::uint32_t>(parent->AsNumber());
        if (oldChild >= remap.size() || oldParent >= remap.size()
            || remap[oldChild] == UINT32_MAX || remap[oldParent] == UINT32_MAX)
            continue;
        hierarchy.push_back(JsonValue(JsonValue::Object{
            { "child", JsonValue(static_cast<double>(remap[oldChild])) },
            { "parent", JsonValue(static_cast<double>(remap[oldParent])) },
        }));
    }

    return JsonValue(JsonValue::Object{
        { "version", JsonValue(1.0) },
        { "entities", JsonValue(std::move(entities)) },
        { "hierarchy", JsonValue(std::move(hierarchy)) },
    });
}
