#include <runtime/spawn/SceneSpawnOverlay.h>

#include <core/composition/CompositionPatch.h>
#include <core/composition/JsonComposition.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <optional>

namespace
{
    [[nodiscard]] bool GivenBySpawn(ComponentTypeId type)
    {
        static const std::array kGiven{ ResolveComponentTypeId<PersistentIdComponent>(),
                                        ResolveComponentTypeId<SceneInstance>(),
                                        ResolveComponentTypeId<LocalTransform>() };
        return std::ranges::find(kGiven, type) != kGiven.end();
    }

    [[nodiscard]] std::optional<PackageEntityId> OnlyRoot(const EntityBuildPackage& package)
    {
        std::optional<PackageEntityId> root;
        const auto entities = package.Entities();
        for (std::uint32_t i = 0; i < entities.size(); ++i)
        {
            if (entities[i].HasParent)
                continue;
            if (root.has_value())
                return std::nullopt;
            root = PackageEntityId{ i };
        }
        return root;
    }
}

SceneSpawnOverlay CaptureSpawnOverlay(const World& world, EntityId root, const EntityBuildPackage& prefab,
                                      const ComponentSerializerRegistry& serializers,
                                      SceneSerializationContext& context)
{
    SceneSpawnOverlay overlay;
    if (const PersistentIdComponent* id = world.IsRegistered<PersistentIdComponent>()
            ? world.TryGet<PersistentIdComponent>(root)
            : nullptr)
        overlay.RootId = id->Id;
    const std::optional<PackageEntityId> prefabRoot = OnlyRoot(prefab);
    for (const auto& serializer : serializers.Entries())
    {
        if (GivenBySpawn(serializer->TypeId()))
            continue;
        JsonWriteArchive archive;
        if (!serializer->Save(archive, root, world, context) || !archive.Ok())
            continue;
        const JsonValue live = archive.TakeValue();
        const PackageComponent* authored =
            prefabRoot.has_value() ? prefab.FindComponent(*prefabRoot, serializer->TypeId()) : nullptr;
        const bool authoredHere = authored != nullptr && authored->SerializedJson.has_value();
        if (live.IsNull())
        {
            if (authoredHere)
                overlay.RootPatch.AsObject().emplace_back(std::string(serializer->JsonKey()), JsonValue());
            continue;
        }
        if (!authoredHere)
        {
            overlay.RootPatch.AsObject().emplace_back(std::string(serializer->JsonKey()), live);
            continue;
        }
        if (std::optional<JsonValue> patch = PatchFrom(*authored->SerializedJson, live, serializer->CompositionSchema()))
            overlay.RootPatch.AsObject().emplace_back(std::string(serializer->JsonKey()), std::move(*patch));
    }
    return overlay;
}

bool ApplySpawnOverlay(EntityBuildPackage& package, const SceneSpawnOverlay& overlay,
                       const ComponentSerializerRegistry& serializers, std::string& error)
{
    if (!overlay.RootPatch.IsObject() || overlay.RootPatch.Size() == 0)
        return true;
    const std::optional<PackageEntityId> root = OnlyRoot(package);
    if (!root.has_value())
    {
        error = "the scene has not exactly one root to give its saved state to";
        return false;
    }
    for (const auto& [key, patch] : overlay.RootPatch.AsObject())
    {
        const IComponentSerializer* serializer = serializers.FindByJsonKey(key);
        if (serializer == nullptr)
        {
            error = "the saved state names component '" + key + "', which this build does not have";
            return false;
        }
        if (patch.IsNull())
        {
            (void)package.RemoveComponent(*root, serializer->TypeId());
            continue;
        }
        const PackageComponent* authored = package.FindComponent(*root, serializer->TypeId());
        if (authored == nullptr || !authored->SerializedJson.has_value())
        {
            (void)package.SetSerializedComponent(*root, serializer->TypeId(), patch);
            continue;
        }
        const CompositionLayerInput<JsonValue> base{ .Value = &*authored->SerializedJson, .Asset = {}, .Provenance = nullptr };
        const CompositionLayerInput<JsonValue> local{ .Value = &patch, .Asset = {}, .Provenance = nullptr };
        CompositionPolicy policy;
        policy.Schema = serializer->CompositionSchema();
        CompositionReport report;
        JsonValue composed = ComposeLayers<JsonValue>(&base, {}, local, policy, report);
        (void)package.SetSerializedComponent(*root, serializer->TypeId(), std::move(composed));
    }
    return true;
}
