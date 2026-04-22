#include "LevelScene.h"

#include <world/transform/TransformHierarchyService.h>

#include <algorithm>

LevelScene::LevelScene(Registry& registry)
    : Registry_(registry)
{
}

EntityId LevelScene::CreateBrush(Vec3d position, Vec3d halfExtents)
{
    EntityId entity = Registry_.Entities.Create();
    Registry_.Resources.Get<TransformHierarchyService>().Register(entity);

    Transform3f transform = Transform3f::Identity();
    transform.Position = position;
    Registry_.Components.Get<TransformStore<Transform3f>>().Add(entity, transform);
    Registry_.Components.Get<BrushComponentStore>().Add(entity, BrushComponent{ .HalfExtents = halfExtents });

    Entities.push_back(entity);
    return entity;
}

EntityId LevelScene::CreateCamera(Vec3d position)
{
    EntityId entity = Registry_.Entities.Create();
    Registry_.Resources.Get<TransformHierarchyService>().Register(entity);

    Transform3f transform = Transform3f::Identity();
    transform.Position = position;
    Registry_.Components.Get<TransformStore<Transform3f>>().Add(entity, transform);
    Registry_.Components.Get<CameraStore>().Add(entity, CameraComponent{});

    Entities.push_back(entity);
    return entity;
}

void LevelScene::DestroyEntity(EntityId entity)
{
    if (!Registry_.Entities.IsAlive(entity))
        return;

    if (auto* cameras = Registry_.Components.TryGet<CameraStore>())
        cameras->Remove(entity);
    if (auto* brushes = Registry_.Components.TryGet<BrushComponentStore>())
        brushes->Remove(entity);
    if (auto* transforms = Registry_.Components.TryGet<TransformStore<Transform3f>>())
        transforms->Remove(entity);
    if (auto* hierarchy = Registry_.Resources.TryGet<TransformHierarchyService>())
        hierarchy->Unregister(entity);

    Registry_.Entities.Destroy(entity);
    std::erase(Entities, entity);
}

void LevelScene::SetTransform(EntityId entity, const Transform3f& transform)
{
    if (auto* transforms = Registry_.Components.TryGet<TransformStore<Transform3f>>())
        transforms->SetLocal(entity, transform);
}

void LevelScene::SetBrushHalfExtents(EntityId entity, Vec3d halfExtents)
{
    if (auto* brushes = Registry_.Components.TryGet<BrushComponentStore>())
        if (BrushComponent* brush = brushes->TryGet(entity))
            brush->HalfExtents = halfExtents;
}

bool LevelScene::HasEntity(EntityId entity) const
{
    return Registry_.Entities.IsAlive(entity);
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
    const auto* transforms = Registry_.Components.TryGet<TransformStore<Transform3f>>();
    return transforms != nullptr ? transforms->TryGetLocal(entity) : nullptr;
}

const BrushComponent* LevelScene::TryGetBrush(EntityId entity) const
{
    const auto* brushes = Registry_.Components.TryGet<BrushComponentStore>();
    return brushes != nullptr ? brushes->TryGet(entity) : nullptr;
}

const CameraComponent* LevelScene::TryGetCamera(EntityId entity) const
{
    const auto* cameras = Registry_.Components.TryGet<CameraStore>();
    return cameras != nullptr ? cameras->TryGet(entity) : nullptr;
}

Registry& LevelScene::GetRegistry()
{
    return Registry_;
}

const Registry& LevelScene::GetRegistry() const
{
    return Registry_;
}
