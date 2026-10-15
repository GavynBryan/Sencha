#include "SpaceWalls.h"

#include "EditorDocument.h"
#include "EditorScene.h"
#include "EntityChangeSet.h"
#include "EntityNameComponent.h"
#include "SpaceComponents.h"

#include "brush/BrushValidation.h"

#include <core/hash/Fnv1a.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <map>
#include <string>

namespace
{
    const BrushRecord* RecordOf(const EditorScene& scene, EntityId entity)
    {
        const BrushComponent* brush = scene.TryGetBrush(entity);
        return brush != nullptr ? scene.GetBrushMeshStore().FindRecord(brush->Id) : nullptr;
    }

    BrushMesh InWorld(const BrushMesh& mesh, const Transform3f& world)
    {
        BrushMesh placed = mesh;
        for (BrushVertex& vertex : placed.Vertices)
            vertex.Position = world.TransformPoint(vertex.Position);
        return placed;
    }

    Aabb3d WorldBounds(const BrushMesh& mesh, const Transform3f& world)
    {
        Aabb3d bounds = Aabb3d::Empty();
        for (const BrushVertex& vertex : mesh.Vertices)
            bounds.ExpandToInclude(world.TransformPoint(vertex.Position));
        return bounds;
    }

    Aabb3d Grown(Aabb3d bounds, float by)
    {
        bounds.Min = bounds.Min - Vec3d{ by, by, by };
        bounds.Max = bounds.Max + Vec3d{ by, by, by };
        return bounds;
    }

    struct ShelledVolume
    {
        EntityId           Entity;
        const BrushRecord* Record = nullptr;
        const Transform3f* World = nullptr;
        float              Thickness = 0.0f;
        PersistentEntityId Id;
    };

    std::optional<ShelledVolume> Shelled(const EditorScene& scene, EntityId entity)
    {
        const World& components = scene.GetRegistry().Components;
        const Shell* shell = components.TryGet<Shell>(entity);
        const BrushRecord* record = RecordOf(scene, entity);
        const Transform3f* world = scene.TryGetWorldTransform(entity);
        if (shell == nullptr || record == nullptr || world == nullptr || !components.HasComponent<Space>(entity)
            || scene.RoleOf(entity) != BrushRole::Volume)
            return std::nullopt;
        return ShelledVolume{ entity, record, world, shell->Thickness, scene.PersistentIdOf(entity) };
    }

    // The shelled volumes near enough to share a wall with `self`: within its
    // thickness of its bounds. Every volume is visited, so this is
    // O(volumes x vertices) per call (docs/deferred.md).
    std::vector<ShelledVolume> NeighboursOf(const EditorScene& scene, const ShelledVolume& self)
    {
        const Aabb3d reach = Grown(WorldBounds(self.Record->Mesh, *self.World), self.Thickness + 1e-2f);
        std::vector<ShelledVolume> neighbours;
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (entity == self.Entity)
                continue;
            const std::optional<ShelledVolume> other = Shelled(scene, entity);
            if (other && reach.Intersects(WorldBounds(other->Record->Mesh, *other->World)))
                neighbours.push_back(*other);
        }
        return neighbours;
    }

    // The openings whose sill is near enough to `self` to cut its walls.
    std::vector<ShellOpening> OpeningsNear(const EditorScene& scene, const ShelledVolume& self,
                                           std::vector<EntityId>& entities)
    {
        const Aabb3d reach = Grown(WorldBounds(self.Record->Mesh, *self.World),
                                   self.Thickness + kOpeningReach + 0.01f);
        std::vector<ShellOpening> openings;
        const World& components = scene.GetRegistry().Components;
        for (const EntityId entity : scene.GetAllEntities())
        {
            const Opening* opening = components.TryGet<Opening>(entity);
            const Transform3f* world = scene.TryGetWorldTransform(entity);
            if (opening == nullptr || world == nullptr || !reach.Contains(world->Position))
                continue;
            ShellOpening cut;
            cut.Origin = world->Position;
            cut.Across = world->Rotation.RotateVector(Vec3d{ 1, 0, 0 });
            cut.Up = world->Rotation.RotateVector(Vec3d{ 0, 1, 0 });
            cut.Through = world->Rotation.RotateVector(Vec3d{ 0, 0, 1 });
            cut.Width = opening->Width;
            cut.Height = opening->Height;
            cut.Shape = opening->Shape;
            cut.Params.ArchRise = opening->ArchRise;
            openings.push_back(cut);
            entities.push_back(entity);
        }
        return openings;
    }

    // Persisted in GeneratedWall, so hashed here from the saved values rather
    // than by the in-process brush signatures, which may change between builds.
    void HashShape(std::uint64_t& hash, const BrushMesh& mesh)
    {
        for (const BrushVertex& vertex : mesh.Vertices)
            HashFnv1aValue(hash, vertex.Position);
        for (const BrushFace& face : mesh.Faces)
        {
            HashFnv1aValue(hash, face.Loop.size());
            HashFnv1aBytes(hash, face.Loop.data(), face.Loop.size() * sizeof(std::uint32_t));
        }
    }

    std::uint32_t Fold(std::uint64_t hash)
    {
        return std::max<std::uint32_t>(static_cast<std::uint32_t>(hash ^ (hash >> 32)), 1);
    }

    std::uint32_t ShapeDigest(const BrushMesh& mesh, const Transform3f& local)
    {
        std::uint64_t hash = kFnv1aOffsetBasis;
        HashShape(hash, mesh);
        HashFnv1aValue(hash, local.Position);
        HashFnv1aValue(hash, local.Rotation);
        HashFnv1aValue(hash, local.Scale);
        return Fold(hash);
    }

    std::uint32_t WallDigest(const EditorScene& scene, EntityId wall)
    {
        const BrushRecord* record = RecordOf(scene, wall);
        const Transform3f* local = scene.TryGetLocalTransform(wall);
        return record != nullptr && local != nullptr ? ShapeDigest(record->Mesh, *local) : 0;
    }

    std::uint32_t Slot(std::uint32_t face, std::uint32_t piece, ShellSide side)
    {
        return (face << 16) | ((piece & 0xFFu) << 8) | static_cast<std::uint32_t>(side);
    }

    std::string_view SideName(const Vec3d& outward)
    {
        if (outward.Y > 0.7f)
            return "ceiling";
        if (outward.Y < -0.7f)
            return "floor";
        if (std::abs(outward.Z) >= std::abs(outward.X))
            return outward.Z < 0.0f ? "north wall" : "south wall";
        return outward.X > 0.0f ? "east wall" : "west wall";
    }

    std::string NameOf(const World& components, EntityId entity)
    {
        const auto* name = components.TryGet<EntityNameComponent>(entity);
        return name != nullptr ? std::string(name->Value.View()) : std::string{};
    }

    std::string WallName(const EditorScene& scene, const SpaceShell& shell, EntityId space, const ShellSlab& slab,
                         ShellSide side)
    {
        const World& components = scene.GetRegistry().Components;
        switch (side)
        {
        case ShellSide::Room:
            return NameOf(components, space) + " " + std::string(SideName(slab.Outward));
        case ShellSide::NeighbourRoom:
            return NameOf(components, shell.Neighbours[*slab.SharedWith]) + " "
                + std::string(SideName(slab.Outward * -1.0f));
        case ShellSide::Outside:
            break;
        }
        return NameOf(components, space) + " " + std::string(SideName(slab.Outward)) + " outside";
    }

    // Paint belongs to the wall once it is built: each new face takes the
    // material of the old face in its plane, so a rebuild keeps a repaint.
    void CarryPaint(const BrushMesh& from, BrushMesh& to)
    {
        constexpr float kSamePlane = 1e-3f;
        for (BrushFace& face : to.Faces)
        {
            const Vec3d normal = BrushComputeFaceNormal(to, face);
            const float offset = normal.Dot(to.Vertices[face.Loop.front()].Position);
            for (const BrushFace& old : from.Faces)
            {
                const Vec3d oldNormal = BrushComputeFaceNormal(from, old);
                if (normal.Dot(oldNormal) > 1.0f - kSamePlane
                    && std::abs(oldNormal.Dot(from.Vertices[old.Loop.front()].Position) - offset) < kSamePlane)
                {
                    face.Material = old.Material;
                    break;
                }
            }
        }
    }

    // One wall brush the shell calls for, in the space's frame.
    struct PlannedWall
    {
        std::uint32_t Slot = 0;
        std::uint32_t Slab = 0;
        ShellSide     Side = ShellSide::Room;
        BrushMesh     Mesh;
        std::uint32_t Digest = 0;
    };

    std::vector<PlannedWall> PlanWalls(const EditorScene& scene, EntityId space, const SpaceShell& shell)
    {
        // A face split among neighbours gives several slabs; each is a piece of it.
        std::vector<std::uint32_t> pieces(shell.Result.Slabs.size());
        std::map<std::uint32_t, std::uint32_t> perFace;
        for (std::size_t slab = 0; slab < shell.Result.Slabs.size(); ++slab)
            pieces[slab] = perFace[shell.Result.Slabs[slab].SourceFace]++;

        const Transform3f world = *scene.TryGetWorldTransform(space);
        std::vector<PlannedWall> planned;
        for (ShellSurface& surface : ShellSurfaces(shell.Result))
        {
            for (BrushVertex& vertex : surface.Mesh.Vertices)
                vertex.Position = world.InverseTransformPoint(vertex.Position);
            (void)BrushValidateAndRepair(surface.Mesh, 0.0f);
            const std::uint32_t digest = ShapeDigest(surface.Mesh, Transform3f::Identity());
            planned.push_back({ Slot(shell.Result.Slabs[surface.Slab].SourceFace, pieces[surface.Slab], surface.Side),
                                surface.Slab, surface.Side, std::move(surface.Mesh), digest });
        }
        return planned;
    }

    const PlannedWall* PlannedAt(const std::vector<PlannedWall>& planned, std::uint32_t slot)
    {
        const auto found = std::ranges::find(planned, slot, &PlannedWall::Slot);
        return found != planned.end() ? &*found : nullptr;
    }
}

std::uint32_t SpaceShellInputs(const EditorScene& scene, EntityId space)
{
    const std::optional<ShelledVolume> self = Shelled(scene, space);
    if (!self)
        return 0;
    std::uint64_t hash = kFnv1aOffsetBasis;
    const auto fold = [&hash](const ShelledVolume& volume) {
        HashFnv1aValue(hash, volume.Id.Value);
        HashShape(hash, volume.Record->Mesh);
        for (const BrushFace& face : volume.Record->Mesh.Faces)
        {
            HashFnv1aValue(hash, face.Material.Material.Id.Value);
            HashFnv1aBytes(hash, face.Material.Material.Path.data(), face.Material.Material.Path.size());
        }
        HashFnv1aValue(hash, volume.Thickness);
        HashFnv1aValue(hash, volume.World->Position);
        HashFnv1aValue(hash, volume.World->Rotation);
        HashFnv1aValue(hash, volume.World->Scale);
    };
    fold(*self);
    for (const ShelledVolume& neighbour : NeighboursOf(scene, *self))
        fold(neighbour);
    std::vector<EntityId> entities;
    const std::vector<ShellOpening> openings = OpeningsNear(scene, *self, entities);
    for (std::size_t i = 0; i < openings.size(); ++i)
    {
        const ShellOpening& opening = openings[i];
        HashFnv1aValue(hash, scene.PersistentIdOf(entities[i]).Value);
        for (const Vec3d& axis : { opening.Origin, opening.Across, opening.Up })
            HashFnv1aValue(hash, axis);
        HashFnv1aValue(hash, opening.Width);
        HashFnv1aValue(hash, opening.Height);
        HashFnv1aValue(hash, opening.Shape);
        HashFnv1aValue(hash, opening.Params.ArchRise);
    }
    HashFnv1aValue(hash, scene.OpenFaceMaterial().Id.Value);
    HashFnv1aBytes(hash, scene.OpenFaceMaterial().Path.data(), scene.OpenFaceMaterial().Path.size());
    return Fold(hash);
}

std::optional<SpaceShell> BuildSpaceShell(const EditorScene& scene, EntityId space)
{
    const std::optional<ShelledVolume> self = Shelled(scene, space);
    if (!self)
        return std::nullopt;
    // Walls are measured in the world, where up and the thickness mean what they say.
    const std::vector<ShelledVolume> near = NeighboursOf(scene, *self);
    std::vector<BrushMesh> nearInWorld;
    nearInWorld.reserve(near.size());
    std::vector<ShellNeighbour> neighbours;
    SpaceShell shell;
    for (const ShelledVolume& neighbour : near)
    {
        nearInWorld.push_back(InWorld(neighbour.Record->Mesh, *neighbour.World));
        neighbours.push_back({ &nearInWorld.back(), neighbour.Thickness, self->Id.Value < neighbour.Id.Value });
        shell.Neighbours.push_back(neighbour.Entity);
    }
    ShellRules rules;
    rules.Thickness = self->Thickness;
    rules.OpenFaceMaterial = scene.OpenFaceMaterial();
    shell.Result = GenerateShell(InWorld(self->Record->Mesh, *self->World), rules, neighbours);

    std::vector<EntityId> openingEntities;
    const std::vector<ShellOpening> openings = OpeningsNear(scene, *self, openingEntities);
    for (const EntityId opening : openingEntities)
        shell.Openings.push_back(scene.PersistentIdOf(opening));
    CutOpenings(shell.Result, self->Thickness, openings);
    for (const std::uint32_t index : shell.Result.SharedWith)
        shell.SharedWith.push_back(near[index].Id);
    shell.Inputs = SpaceShellInputs(scene, space);
    return shell;
}

std::vector<EntityId> SpaceWalls(const EditorScene& scene, EntityId space)
{
    std::vector<EntityId> walls;
    const World& components = scene.GetRegistry().Components;
    for (const EntityId entity : scene.GetAllEntities())
        if (components.HasComponent<GeneratedWall>(entity) && scene.GetParent(entity) == space)
            walls.push_back(entity);
    return walls;
}

SpaceWallDrift CompareSpaceWalls(const EditorScene& scene, EntityId space, const SpaceShell& shell)
{
    const std::vector<PlannedWall> planned = PlanWalls(scene, space, shell);
    const World& components = scene.GetRegistry().Components;
    SpaceWallDrift drift;
    std::set<std::uint32_t> claimed;
    for (const EntityId wall : SpaceWalls(scene, space))
    {
        const GeneratedWall& built = *components.TryGet<GeneratedWall>(wall);
        // A second wall in one slot is no wall of the plan's.
        const PlannedWall* plan = claimed.insert(built.Slot).second ? PlannedAt(planned, built.Slot) : nullptr;
        // A wall reshaped by hand is kept from the next rebuild on.
        if (built.State == WallState::Kept || WallDigest(scene, wall) != built.Built)
        {
            if (plan == nullptr || plan->Digest != built.Built)
                drift.KeptBehind.push_back(wall);
        }
        else if (plan == nullptr || plan->Digest != built.Built)
            drift.Stale = true;
    }
    drift.Stale = drift.Stale || std::ranges::any_of(planned, [&](const PlannedWall& plan) {
        return !claimed.contains(plan.Slot);
    });
    return drift;
}

bool SpaceWallsStale(const EditorScene& scene, EntityId space)
{
    const std::optional<SpaceShell> shell = BuildSpaceShell(scene, space);
    return shell && CompareSpaceWalls(scene, space, *shell).Stale;
}

void RebuildSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder, EntityId space, bool reclaim)
{
    EditorScene& scene = document.GetScene();
    const std::optional<SpaceShell> shell = BuildSpaceShell(scene, space);
    if (!shell)
        return;
    World& components = scene.GetRegistry().Components;
    // A second wall in one slot (a file edited by hand) is left over like a slot that went.
    std::map<std::uint32_t, EntityId> unclaimed;
    std::vector<EntityId> leftover;
    for (const EntityId wall : SpaceWalls(scene, space))
        if (!unclaimed.emplace(components.TryGet<GeneratedWall>(wall)->Slot, wall).second)
            leftover.push_back(wall);

    bool wrote = false;
    for (PlannedWall& plan : PlanWalls(scene, space, *shell))
    {
        const auto found = unclaimed.find(plan.Slot);
        if (found == unclaimed.end())
        {
            const EntityId wall = scene.CreateBrushFromMesh(Transform3f::Identity(), std::move(plan.Mesh));
            (void)scene.SetParent(wall, space);
            components.TryGet<EntityNameComponent>(wall)->Value = InlineString<64>(
                scene.NextEntityName(WallName(scene, *shell, space, shell->Result.Slabs[plan.Slab], plan.Side)));
            components.AddComponent(wall, GeneratedWall{ plan.Slot, plan.Digest, WallState::Generated });
            recorder.Created(wall);
            wrote = true;
            continue;
        }
        const EntityId wall = found->second;
        unclaimed.erase(found);
        GeneratedWall& built = *components.TryGet<GeneratedWall>(wall);
        const bool reshaped = WallDigest(scene, wall) != built.Built;
        if (!reclaim && built.State == WallState::Generated && reshaped)
        {
            recorder.Touch(wall);
            built.State = WallState::Kept;
            wrote = true;
        }
        if (!reclaim && built.State == WallState::Kept)
            continue;
        if (built.State == WallState::Generated && !reshaped && built.Built == plan.Digest)
            continue;
        recorder.Touch(wall);
        CarryPaint(*scene.TryGetBrushMesh(wall), plan.Mesh);
        scene.SetTransform(wall, Transform3f::Identity());
        scene.SetBrushMesh(wall, std::move(plan.Mesh));
        built = GeneratedWall{ plan.Slot, plan.Digest, WallState::Generated };
        wrote = true;
    }

    for (const auto& [slot, wall] : unclaimed)
        leftover.push_back(wall);
    for (const EntityId wall : leftover)
    {
        if (!reclaim && components.TryGet<GeneratedWall>(wall)->State == WallState::Kept)
            continue;
        recorder.Touch(wall);
        scene.DestroyEntity(wall);
        wrote = true;
    }
    scene.SetCheckedWallInputs(space, shell->Inputs);
    if (!wrote)
        return;
    scene.RefreshDerivedTransforms();
    document.MarkDirty();
}

void RebuildStaleSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder)
{
    std::vector<EntityId> moved;
    const EditorScene& scene = document.GetScene();
    for (const EntityId entity : scene.GetAllEntities())
        if (const std::uint32_t inputs = SpaceShellInputs(scene, entity);
            inputs != 0 && inputs != scene.CheckedWallInputs(entity))
            moved.push_back(entity);
    for (const EntityId space : moved)
        RebuildSpaceWalls(document, recorder, space);
}
