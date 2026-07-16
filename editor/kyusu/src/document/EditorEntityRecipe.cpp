#include "EditorEntityRecipe.h"

#include "document/WorldDocument.h"

#include <core/serialization/JsonArchive.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace
{

Quatf RotationFromLocalZ(Vec3d normal)
{
    if (normal.SqrMagnitude() <= 1e-8f)
        return Quatf::Identity();
    const Vec3d from = -Vec3d::Forward();
    const Vec3d to = normal.Normalized();
    const float dot = std::clamp(from.Dot(to), -1.0f, 1.0f);
    if (dot > 1.0f - 1e-6f)
        return Quatf::Identity();
    if (dot < -1.0f + 1e-6f)
        return Quatf::FromAxisAngle(Vec3d::Up(), 3.14159265358979323846f);
    const Vec3d axis = from.Cross(to);
    return Quatf{ axis.X, axis.Y, axis.Z, 1.0f + dot }.Normalized();
}

template <typename Component>
void AppendComponent(EntitySnapshot& snapshot, Registry& registry, EntityId entity,
                     LoggingProvider& logging)
{
    for (const auto& serializer : GetComponentSerializerEntries())
    {
        if (serializer->TypeId() != ResolveComponentTypeId<Component>())
            continue;
        SceneSerializationContext context(logging);
        JsonWriteArchive archive;
        if (!serializer->Save(archive, entity, registry, context) || !archive.Ok())
            return;
        snapshot.Components.AsObject().emplace_back(
            std::string(serializer->JsonKey()), archive.TakeValue());
        return;
    }
}

template <typename Component>
EntitySnapshot BuildSnapshot(WorldDocument& world, LocalTransform transform,
                             Component component)
{
    Registry registry;
    registry.Components.RegisterComponent<LocalTransform>();
    registry.Components.RegisterComponent<WorldTransform>();
    registry.Components.RegisterComponent<Component>();
    const EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, transform);
    registry.Components.AddComponent(entity, WorldTransform{ transform.Value });
    registry.Components.AddComponent(entity, component);

    EntitySnapshot snapshot;
    snapshot.Components = JsonValue{ JsonValue::Object{} };
    AppendComponent<LocalTransform>(snapshot, registry, entity, world.Logging());
    AppendComponent<Component>(snapshot, registry, entity, world.Logging());
    return snapshot;
}

template <typename Component>
EntitySnapshot BuildSnapshot(WorldDocument& world, Component component)
{
    Registry registry;
    registry.Components.RegisterComponent<Component>();
    const EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, component);

    EntitySnapshot snapshot;
    snapshot.Components = JsonValue{ JsonValue::Object{} };
    AppendComponent<Component>(snapshot, registry, entity, world.Logging());
    return snapshot;
}

ZoneId ResolveOtherSide(const WorldPartitionManifest& manifest, ZoneId active,
                        Vec3d point, Vec3d normal)
{
    const Vec3d sample = point + normal.Normalized() * 1.0f;
    ZoneId result;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == active || !zone.Bounds.Contains(sample))
            continue;
        if (result.IsValid())
            return {};
        result = zone.Id;
    }
    return result;
}

JsonValue* MutableFind(JsonValue& object, std::string_view key)
{
    if (!object.IsObject())
        return nullptr;
    for (auto& [name, value] : object.AsObject())
        if (name == key)
            return &value;
    return nullptr;
}

} // namespace

void RemapWorldConnectionDuplicateSnapshots(
    WorldDocument& world, std::vector<EntitySnapshot>& snapshots)
{
    std::unordered_map<std::string, std::string> dockIds;
    std::unordered_map<std::string, std::string> linkIds;
    std::unordered_set<uint64_t> mintedIds;

    const auto remapIdentity = [&](EntitySnapshot& snapshot, std::string_view component,
                                   auto mint, auto stringify, auto& remaps)
    {
        JsonValue* value = MutableFind(snapshot.Components, component);
        JsonValue* id = value != nullptr ? MutableFind(*value, "id") : nullptr;
        if (id == nullptr || !id->IsString())
            return;
        const std::string before = id->AsString();
        auto next = (world.*mint)();
        while (!mintedIds.insert(next.Value).second)
            next = (world.*mint)();
        const std::string after = stringify(next);
        *id = JsonValue{ after };
        remaps.emplace(before, after);
    };

    for (EntitySnapshot& snapshot : snapshots)
    {
        remapIdentity(snapshot, "World Dock", &WorldDocument::MintDockId,
                      DockIdToString, dockIds);
        remapIdentity(snapshot, "World Link", &WorldDocument::MintLinkId,
                      LinkIdToString, linkIds);
    }

    for (EntitySnapshot& snapshot : snapshots)
    {
        JsonValue* binding = MutableFind(snapshot.Components, "Dock Gate Binding");
        JsonValue* dock = binding != nullptr ? MutableFind(*binding, "dock") : nullptr;
        if (dock == nullptr || !dock->IsString())
            continue;
        const auto it = dockIds.find(dock->AsString());
        if (it != dockIds.end())
            *dock = JsonValue{ it->second };
    }
}

bool EditorEntityRecipeRegistry::Register(std::string name,
                                          std::unique_ptr<IEditorEntityRecipe> recipe)
{
    if (name.empty() || recipe == nullptr || Recipes.contains(name))
        return false;
    Recipes.emplace(std::move(name), std::move(recipe));
    return true;
}

const IEditorEntityRecipe* EditorEntityRecipeRegistry::Find(std::string_view name) const
{
    const auto it = Recipes.find(std::string(name));
    return it == Recipes.end() ? nullptr : it->second.get();
}

EntitySnapshot WorldDockRecipe::Build(const EditorCreateContext& context) const
{
    if (context.World == nullptr || !context.ActiveZone.IsValid())
        return {};
    Vec3d normal = context.PlacementNormal;
    if (normal.SqrMagnitude() <= 1e-8f)
        normal = -Vec3d::Forward();
    normal = normal.Normalized();

    WorldDock dock;
    dock.Id = context.World->MintDockId();
    dock.ZoneA = context.ActiveZone;
    dock.ZoneB = ResolveOtherSide(context.World->Manifest(), context.ActiveZone,
                                  context.PlacementPoint, normal);
    LocalTransform transform;
    transform.Value.Position = context.PlacementPoint;
    transform.Value.Rotation = RotationFromLocalZ(normal);
    return BuildSnapshot(*context.World, transform, dock);
}

EntitySnapshot WorldLinkRecipe::Build(const EditorCreateContext& context) const
{
    if (context.World == nullptr || !context.ActiveZone.IsValid()
        || !ZoneB.IsValid() || context.ActiveZone == ZoneB)
        return {};
    WorldLink link;
    link.Id = context.World->MintLinkId();
    link.ZoneA = context.ActiveZone;
    link.ZoneB = ZoneB;
    return BuildSnapshot(*context.World, link);
}
