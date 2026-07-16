#include "EditorComponentAdapter.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "document/commands/SetZoneBoundsCommand.h"
#include "selection/SelectionService.h"

#include <world/serialization/SceneSerializer.h>

#include <algorithm>
#include <cmath>

namespace
{
class ZoneBoundsTransaction final : public IValueEditTransaction<Aabb3d>
{
public:
    ZoneBoundsTransaction(WorldDocument& world, CommandStack& commands, ZoneId zone,
                          Aabb3d before, bool beforeOverride)
        : World(world), Commands(commands), Zone(zone), Before(before)
        , BeforeOverride(beforeOverride) {}

    void Preview(const Aabb3d& value) override
    {
        for (ZoneHeader& header : World.Manifest().Zones)
            if (header.Id == Zone)
            {
                header.Bounds = value;
                header.BoundsOverridden = true;
                return;
            }
    }

    void Commit(const Aabb3d& value) override
    {
        Preview(Before);
        for (ZoneHeader& header : World.Manifest().Zones)
            if (header.Id == Zone)
                header.BoundsOverridden = BeforeOverride;
        Commands.Execute(std::make_unique<SetZoneBoundsCommand>(
            World, Zone, Before, BeforeOverride, value, true));
    }

    void Cancel() override
    {
        for (ZoneHeader& header : World.Manifest().Zones)
            if (header.Id == Zone)
            {
                header.Bounds = Before;
                header.BoundsOverridden = BeforeOverride;
                return;
            }
    }

private:
    WorldDocument& World;
    CommandStack& Commands;
    ZoneId Zone;
    Aabb3d Before;
    bool BeforeOverride = false;
};

std::optional<float> IntersectRectangle(const Ray3d& ray,
                                        const EditorPickProxy& proxy)
{
    const Vec3d normal = -proxy.LocalToWorld.Forward();
    const float denominator = ray.Direction.Dot(normal);
    if (std::abs(denominator) <= 1.0e-6f)
        return std::nullopt;
    const float distance = (proxy.LocalToWorld.Position - ray.Origin).Dot(normal)
        / denominator;
    if (distance < 0.0f)
        return std::nullopt;
    const Vec3d delta = ray.PointAt(distance) - proxy.LocalToWorld.Position;
    if (std::abs(delta.Dot(proxy.LocalToWorld.Right())) > proxy.HalfExtents.X
        || std::abs(delta.Dot(proxy.LocalToWorld.Up())) > proxy.HalfExtents.Y)
        return std::nullopt;
    return distance;
}

std::optional<float> IntersectBox(const Ray3d& ray, const EditorPickProxy& proxy)
{
    const Vec3d axes[3] = { proxy.LocalToWorld.Right(), proxy.LocalToWorld.Up(),
                            -proxy.LocalToWorld.Forward() };
    const Vec3d delta = ray.Origin - proxy.LocalToWorld.Position;
    Vec3d origin{ delta.Dot(axes[0]), delta.Dot(axes[1]), delta.Dot(axes[2]) };
    Vec3d direction{ ray.Direction.Dot(axes[0]), ray.Direction.Dot(axes[1]),
                     ray.Direction.Dot(axes[2]) };
    float near = 0.0f;
    float far = 1.0e6f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) <= 1.0e-6f)
        {
            if (origin[axis] < proxy.Bounds.Min[axis]
                || origin[axis] > proxy.Bounds.Max[axis])
                return std::nullopt;
            continue;
        }
        float a = (proxy.Bounds.Min[axis] - origin[axis]) / direction[axis];
        float b = (proxy.Bounds.Max[axis] - origin[axis]) / direction[axis];
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
        if (near > far)
            return std::nullopt;
    }
    return near;
}
}

bool EditorComponentAdapterRegistry::Register(
    std::unique_ptr<IEditorComponentAdapter> adapter)
{
    if (adapter == nullptr || Adapters.contains(adapter->Type()))
        return false;
    Adapters.emplace(adapter->Type(), std::move(adapter));
    return true;
}

const IEditorComponentAdapter* EditorComponentAdapterRegistry::Find(
    ComponentTypeId type) const
{
    const auto it = Adapters.find(type);
    return it == Adapters.end() ? nullptr : it->second.get();
}

EditorAffordanceService::EditorAffordanceService(
    WorldDocument& world, SelectionService& selection, CommandStack& commands,
    GridSettings& grid)
    : World(world), Selection(selection), Commands(commands), Grid(grid)
{
}

void EditorAffordanceService::Build(ViewportAffordanceOutput& output) const
{
    EditorDocument& document = World.FocusDocument();
    EditorScene& scene = document.GetScene();
    const ::Registry& registry = scene.GetRegistry();
    const SelectableRef selected = Selection.GetPrimarySelection();

    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        for (const auto& serializer : GetComponentSerializerEntries())
        {
            const IEditorComponentAdapter* adapter = Adapters.Find(serializer->TypeId());
            if (adapter == nullptr || !serializer->HasComponent(entity, registry))
                continue;
            adapter->BuildViewport(EditorComponentContext{
                .World = World,
                .Document = document,
                .Scene = scene,
                .Commands = Commands,
                .Entity = entity,
                .Transform = transform != nullptr ? *transform : Transform3f::Identity(),
                .Selected = selected.IsEntity() && selected.Registry == registry.Id
                    && selected.Entity == entity,
            }, output);
        }
    }

    AppendZoneBoundsTarget(output);
}

void EditorAffordanceService::AppendZoneBoundsTarget(
    ViewportAffordanceOutput& output) const
{
    if (!World.IsWorld() || !Selection.GetSelection().empty())
        return;
    const ZoneHeader* zone = nullptr;
    for (const ZoneHeader& candidate : World.Manifest().Zones)
        if (candidate.Id == World.FocusZone())
        {
            zone = &candidate;
            break;
        }
    if (zone == nullptr || !zone->Bounds.IsValid())
        return;
    const ZoneId id = zone->Id;
    const Aabb3d bounds = zone->Bounds;
    const bool overridden = zone->BoundsOverridden;
    output.Aabbs.push_back(AabbEditTarget{
        .Key = id.Value,
        .LocalToWorld = Transform3f::Identity(),
        .Value = bounds,
        .BeginEdit = [this, id, bounds, overridden]
        {
            return std::make_unique<ZoneBoundsTransaction>(
                World, Commands, id, bounds, overridden);
        },
    });
}

std::optional<std::pair<SelectableRef, float>> EditorAffordanceService::Pick(
    const Ray3d& ray, const EditorScene& scene) const
{
    if (&scene != &World.FocusDocument().GetScene())
        return std::nullopt;
    ViewportAffordanceOutput output;
    Build(output);
    std::optional<std::pair<SelectableRef, float>> best;
    for (const EditorPickProxy& proxy : output.PickProxies)
    {
        const std::optional<float> distance = proxy.Shape == EditorPickProxy::Kind::Rectangle
            ? IntersectRectangle(ray, proxy) : IntersectBox(ray, proxy);
        if (!distance || (best && best->second <= *distance))
            continue;
        best = std::pair{ SelectableRef::EntitySelection(scene.GetRegistry().Id,
                                                          proxy.Entity),
                          *distance };
    }
    return best;
}

bool EditorAffordanceService::AllowsScaleForSelection() const
{
    const SelectableRef selected = Selection.GetPrimarySelection();
    if (!selected.IsEntity())
        return true;
    const ::Registry& registry = World.FocusDocument().GetRegistry();
    if (selected.Registry != registry.Id)
        return true;
    for (const auto& serializer : GetComponentSerializerEntries())
        if (serializer->HasComponent(selected.Entity, registry))
            if (const IEditorComponentAdapter* adapter = Adapters.Find(serializer->TypeId());
                adapter != nullptr && !adapter->AllowEntityScale())
                return false;
    return true;
}

bool EditorAffordanceService::HasEditTargets() const
{
    ViewportAffordanceOutput output;
    Build(output);
    return !output.Aabbs.empty() || !output.Rectangles.empty();
}
