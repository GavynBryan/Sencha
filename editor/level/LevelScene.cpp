#include "LevelScene.h"

#include "brush/BrushOps.h"

#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

LevelScene::LevelScene(Registry& registry)
    : Registry_(registry)
{
}

EntityId LevelScene::CreateBrush(Vec3d position, Vec3d halfExtents)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;
    return CreateBrushFromMesh(transform, BrushOps::MakeBox(halfExtents));
}

EntityId LevelScene::CreateBrushFromMesh(const Transform3f& transform, BrushMesh mesh)
{
    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });
    world.AddComponent(entity, BrushComponent{ BrushMeshes.Create(std::move(mesh)) });

    Entities.push_back(entity);
    return entity;
}

EntityId LevelScene::CreateCamera(Vec3d position)
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

EntityId LevelScene::CreateEntity(Vec3d position)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;

    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });

    Entities.push_back(entity);
    return entity;
}

void LevelScene::DestroyEntity(EntityId entity)
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

void LevelScene::TrackEntity(EntityId entity)
{
    // Adopt an entity created outside the Create* helpers (a restored deletion)
    // into the tracked list, without the full-list reorder SyncFromRegistry does.
    if (std::find(Entities.begin(), Entities.end(), entity) == Entities.end())
        Entities.push_back(entity);
}

void LevelScene::SetTransform(EntityId entity, const Transform3f& transform)
{
    if (LocalTransform* local = Registry_.Components.TryGet<LocalTransform>(entity))
        local->Value = transform;
}

void LevelScene::SetBrushHalfExtents(EntityId entity, Vec3d halfExtents)
{
    SetBrushMesh(entity, BrushOps::MakeBox(halfExtents));
}

void LevelScene::SetBrushMesh(EntityId entity, BrushMesh mesh)
{
    if (const BrushComponent* brush = Registry_.Components.TryGet<BrushComponent>(entity))
        BrushMeshes.Set(brush->Id, std::move(mesh));
}

void LevelScene::Clear()
{
    World& world = Registry_.Components;
    for (EntityId entity : world.GetAliveEntities())
        world.DestroyEntity(entity);
    Entities.clear();
    BrushMeshes.Clear();
    HiddenEntities.clear();
    LockedEntities.clear();
}

void LevelScene::SyncFromRegistry()
{
    Entities = Registry_.Components.GetAliveEntities();
}

bool LevelScene::HasEntity(EntityId entity) const
{
    return Registry_.Components.IsAlive(entity);
}

uint32_t LevelScene::GetEntityCount() const
{
    return static_cast<uint32_t>(Entities.size());
}

std::span<const EntityId> LevelScene::GetAllEntities() const
{
    return Entities;
}

const Transform3f* LevelScene::TryGetTransform(EntityId entity) const
{
    const World& world = Registry_.Components;
    const LocalTransform* local = world.TryGet<LocalTransform>(entity);
    return local != nullptr ? &local->Value : nullptr;
}

const BrushComponent* LevelScene::TryGetBrush(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<BrushComponent>(entity);
}

const BrushMesh* LevelScene::TryGetBrushMesh(EntityId entity) const
{
    const BrushComponent* brush = TryGetBrush(entity);
    return brush != nullptr ? BrushMeshes.Find(brush->Id) : nullptr;
}

const CameraComponent* LevelScene::TryGetCamera(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<CameraComponent>(entity);
}

Registry& LevelScene::GetRegistry()
{
    return Registry_;
}

const Registry& LevelScene::GetRegistry() const
{
    return Registry_;
}

bool LevelScene::IsEntityVisible(EntityId entity) const
{
    return !HiddenEntities.contains(entity.Index);
}

bool LevelScene::IsEntityLocked(EntityId entity) const
{
    return LockedEntities.contains(entity.Index);
}

void LevelScene::SetEntityVisible(EntityId entity, bool visible)
{
    if (visible)
        HiddenEntities.erase(entity.Index);
    else
        HiddenEntities.insert(entity.Index);
}

void LevelScene::SetEntityLocked(EntityId entity, bool locked)
{
    if (locked)
        LockedEntities.insert(entity.Index);
    else
        LockedEntities.erase(entity.Index);
}
