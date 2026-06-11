#include "LevelScene.h"

#include <world/transform/TransformComponents.h>

#include <algorithm>

LevelScene::LevelScene(Registry& registry)
    : Registry_(registry)
{
}

EntityId LevelScene::CreateBrush(Vec3d position, Vec3d halfExtents)
{
    Transform3f transform = Transform3f::Identity();
    transform.Position = position;

    World& world = Registry_.Components;
    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ transform });
    world.AddComponent(entity, BrushComponent{ .HalfExtents = halfExtents });

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

void LevelScene::DestroyEntity(EntityId entity)
{
    World& world = Registry_.Components;
    if (!world.IsAlive(entity))
        return;

    world.DestroyEntity(entity);
    std::erase(Entities, entity);
}

void LevelScene::SetTransform(EntityId entity, const Transform3f& transform)
{
    if (LocalTransform* local = Registry_.Components.TryGet<LocalTransform>(entity))
        local->Value = transform;
}

void LevelScene::SetBrushHalfExtents(EntityId entity, Vec3d halfExtents)
{
    if (BrushComponent* brush = Registry_.Components.TryGet<BrushComponent>(entity))
        brush->HalfExtents = halfExtents;
}

void LevelScene::Clear()
{
    World& world = Registry_.Components;
    for (EntityId entity : world.GetAliveEntities())
        world.DestroyEntity(entity);
    Entities.clear();
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
