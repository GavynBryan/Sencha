#include "EditorScene.h"

#include "../brush/BrushBounds.h"
#include "../brush/BrushOps.h"

#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

EditorScene::EditorScene(Registry& registry)
    : Registry_(registry)
{
}

EntityId EditorScene::CreateBrush(Vec3d position, Vec3d halfExtents)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;
    return CreateBrushFromMesh(transform, BrushOps::MakeBox(halfExtents));
}

EntityId EditorScene::CreateBrushFromMesh(const Transform3f& transform, BrushMesh mesh)
{
    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });
    world.AddComponent(entity, BrushComponent{ BrushMeshes.Create(std::move(mesh)) });

    Entities.push_back(entity);
    return entity;
}

EntityId EditorScene::CreateCamera(Vec3d position)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;

    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });
    world.AddComponent(entity, CameraComponent{});

    Entities.push_back(entity);
    return entity;
}

EntityId EditorScene::CreateEntity(Vec3d position)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;

    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });

    Entities.push_back(entity);
    return entity;
}

void EditorScene::DestroyEntity(EntityId entity)
{
    World& world = Registry_.Components;
    if (!world.IsAlive(entity))
        return;

    // Free the brush's sidecar mesh; nothing else references it once the entity
    // is gone (DestroyEntity is the single destruction path, including undo).
    if (const BrushComponent* brush = world.TryGet<BrushComponent>(entity))
        BrushMeshes.Destroy(brush->Id);

    world.DestroyEntity(entity);
    std::erase(Entities, entity);
    // Drop any editor flags so a reused slot index starts visible + unlocked.
    HiddenEntities.erase(entity.Index);
    LockedEntities.erase(entity.Index);
}

void EditorScene::TrackEntity(EntityId entity)
{
    // Adopt an entity created outside the Create* helpers (a restored deletion)
    // into the tracked list, without the full-list reorder SyncFromRegistry does.
    if (std::find(Entities.begin(), Entities.end(), entity) == Entities.end())
        Entities.push_back(entity);
}

void EditorScene::SetTransform(EntityId entity, const Transform3f& transform)
{
    if (LocalTransform* local = Registry_.Components.TryGet<LocalTransform>(entity))
        local->Value = transform;
}

void EditorScene::SetBrushHalfExtents(EntityId entity, Vec3d halfExtents)
{
    SetBrushMesh(entity, BrushOps::MakeBox(halfExtents));
}

void EditorScene::SetBrushMesh(EntityId entity, BrushMesh mesh)
{
    if (const BrushComponent* brush = Registry_.Components.TryGet<BrushComponent>(entity))
        BrushMeshes.Set(brush->Id, std::move(mesh));
}

void EditorScene::Clear()
{
    World& world = Registry_.Components;
    for (EntityId entity : world.GetAliveEntities())
        world.DestroyEntity(entity);
    Entities.clear();
    BrushMeshes.Clear();
    HiddenEntities.clear();
    LockedEntities.clear();
}

void EditorScene::SyncFromRegistry()
{
    Entities = Registry_.Components.GetAliveEntities();
}

bool EditorScene::HasEntity(EntityId entity) const
{
    return Registry_.Components.IsAlive(entity);
}

uint32_t EditorScene::GetEntityCount() const
{
    return static_cast<uint32_t>(Entities.size());
}

std::span<const EntityId> EditorScene::GetAllEntities() const
{
    return Entities;
}

const Transform3f* EditorScene::TryGetTransform(EntityId entity) const
{
    const World& world = Registry_.Components;
    const LocalTransform* local = world.TryGet<LocalTransform>(entity);
    return local != nullptr ? &local->Value : nullptr;
}

const BrushComponent* EditorScene::TryGetBrush(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<BrushComponent>(entity);
}

const BrushMesh* EditorScene::TryGetBrushMesh(EntityId entity) const
{
    const BrushComponent* brush = TryGetBrush(entity);
    return brush != nullptr ? BrushMeshes.Find(brush->Id) : nullptr;
}

const CameraComponent* EditorScene::TryGetCamera(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<CameraComponent>(entity);
}

std::optional<Aabb3d> EditorScene::TryGetWorldBounds(EntityId entity) const
{
    const BrushMesh* mesh = TryGetBrushMesh(entity);
    const Transform3f* transform = TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr || mesh->Vertices.empty())
        return std::nullopt;

    return BrushWorldBounds(*mesh, *transform);
}

Registry& EditorScene::GetRegistry()
{
    return Registry_;
}

const Registry& EditorScene::GetRegistry() const
{
    return Registry_;
}

bool EditorScene::IsEntityVisible(EntityId entity) const
{
    return !HiddenEntities.contains(entity.Index);
}

bool EditorScene::IsEntityLocked(EntityId entity) const
{
    return LockedEntities.contains(entity.Index);
}

void EditorScene::SetEntityVisible(EntityId entity, bool visible)
{
    if (visible)
        HiddenEntities.erase(entity.Index);
    else
        HiddenEntities.insert(entity.Index);
}

void EditorScene::SetEntityLocked(EntityId entity, bool locked)
{
    if (locked)
        LockedEntities.insert(entity.Index);
    else
        LockedEntities.erase(entity.Index);
}
