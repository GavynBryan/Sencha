#include "EditorScene.h"

#include "brush/BrushBounds.h"
#include "brush/BrushOps.h"

#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformPropagation.h>

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
    world.AddComponent(entity, PersistentIdComponent{ MintPersistentId() });

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
    world.AddComponent(entity, PersistentIdComponent{ MintPersistentId() });

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
    world.AddComponent(entity, PersistentIdComponent{ MintPersistentId() });

    Entities.push_back(entity);
    return entity;
}

PersistentEntityId EditorScene::MintPersistentId()
{
    while (true)
    {
        const uint64_t value = IdRng_() & ~PersistentEntityIdRuntimeBit;
        if (value != 0 && TakenIds_.insert(value).second)
            return PersistentEntityId{ value };
    }
}

bool EditorScene::EnsurePersistentId(EntityId entity)
{
    World& world = Registry_.Components;
    PersistentIdComponent* id = world.TryGet<PersistentIdComponent>(entity);

    // The entity does not contribute to the index until it is adopted, so a
    // successful insert means its id was genuinely free and it keeps it. A
    // failed insert means another tracked entity already holds the value.
    if (id != nullptr && IsAuthoredPersistentEntityId(id->Id)
        && TakenIds_.insert(id->Id.Value).second)
    {
        return false;
    }

    const PersistentEntityId minted = MintPersistentId();
    if (id != nullptr)
        id->Id = minted;
    else
        world.AddComponent(entity, PersistentIdComponent{ minted });
    return true;
}

bool EditorScene::ValidateIdentities(std::string* error) const
{
    const World& world = Registry_.Components;
    const auto fail = [error](std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    std::unordered_set<uint64_t> seen;
    seen.reserve(Entities.size());
    for (EntityId entity : Entities)
    {
        const auto* id = world.TryGet<PersistentIdComponent>(entity);
        if (id == nullptr || !id->Id.IsValid())
            return fail("an entity has no persistent identity");
        if (!IsAuthoredPersistentEntityId(id->Id))
            return fail("persistent entity id " + PersistentEntityIdToString(id->Id)
                + " uses the reserved runtime namespace");
        if (!seen.insert(id->Id.Value).second)
            return fail("persistent entity id " + PersistentEntityIdToString(id->Id)
                + " is held by two entities");
    }
    return true;
}

void EditorScene::RefreshDerivedTransforms()
{
    World& world = Registry_.Components;
    if (!world.IsRegistered<LocalTransform>() || !world.IsRegistered<WorldTransform>())
        return;

    // Authoring builds an entity component by component, so it arrives carrying
    // a local transform and no derived column. Seeding here rather than in each
    // creation path keeps the pairing under one owner and covers the routes that
    // do not go through Create* at all -- a restored snapshot, a loaded file, a
    // recipe. Once an entity is seeded this costs one lookup and nothing else.
    for (EntityId entity : Entities)
    {
        if (world.TryGet<LocalTransform>(entity) != nullptr
            && world.TryGet<WorldTransform>(entity) == nullptr)
        {
            SeedDerivedWorldTransform(world, entity);
        }
    }

    PropagateTransforms(world);
}

void EditorScene::DestroyEntity(EntityId entity)
{
    World& world = Registry_.Components;
    if (!world.IsAlive(entity))
        return;

    // Free the brush's sidecar mesh, unless another entity still shares it (an
    // instanced brush: several entities carry the same BrushId, live or dormant).
    // DestroyEntity is the single destruction path, including undo.
    const auto sharedElsewhere = [&](BrushId id)
    {
        for (EntityId other : Entities)
        {
            if (other == entity)
                continue;
            if (const BrushComponent* b = world.TryGet<BrushComponent>(other); b != nullptr && b->Id == id)
                return true;
            if (const BakedBrushComponent* bb = world.TryGet<BakedBrushComponent>(other);
                bb != nullptr && bb->Source == id)
                return true;
        }
        return false;
    };
    if (const BrushComponent* brush = world.TryGet<BrushComponent>(entity))
    {
        if (!sharedElsewhere(brush->Id))
            BrushMeshes.Destroy(brush->Id);
    }
    if (const BakedBrushComponent* baked = world.TryGet<BakedBrushComponent>(entity))
    {
        if (!sharedElsewhere(baked->Source))
            BrushMeshes.Destroy(baked->Source);
    }

    // Release the id while the component still exists. Undo of a delete restores
    // the snapshot's id, which only works because destruction frees it here.
    if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
        TakenIds_.erase(id->Id.Value);

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
    // Re-adopting a tracked entity must stay a no-op: its id is already indexed,
    // and running the identity check again would read that as a duplicate.
    if (std::find(Entities.begin(), Entities.end(), entity) != Entities.end())
        return;

    // Identity is settled at adoption, before the entity counts as tracked, so
    // every route into the scene lands here exactly once: a restored snapshot
    // keeps its id when nothing live holds it, and an entity assembled directly
    // in the registry gets one it never minted for itself.
    (void)EnsurePersistentId(entity);
    Entities.push_back(entity);
}

void EditorScene::SetTransform(EntityId entity, const Transform3f& transform)
{
    if (LocalTransform* local = Registry_.Components.TryGet<LocalTransform>(entity))
        local->Value = transform;
}

void EditorScene::SetWorldTransform(EntityId entity, const Transform3f& world)
{
    const World& components = Registry_.Components;
    const Parent* parent = components.TryGet<Parent>(entity);
    const WorldTransform* parentWorld =
        parent != nullptr ? components.TryGet<WorldTransform>(parent->Entity) : nullptr;
    if (parentWorld == nullptr)
    {
        SetTransform(entity, world);
        return;
    }

    // The exact inverse of the parent-times-child composition transform
    // propagation applies, so a value placed here reads back unchanged.
    const Transform3f& frame = parentWorld->Value;
    Transform3f local;
    local.Position = frame.InverseTransformPoint(world.Position);
    local.Rotation = frame.Rotation.Conjugate() * world.Rotation;
    local.Scale = Vec3d(
        frame.Scale.X != 0.0f ? world.Scale.X / frame.Scale.X : world.Scale.X,
        frame.Scale.Y != 0.0f ? world.Scale.Y / frame.Scale.Y : world.Scale.Y,
        frame.Scale.Z != 0.0f ? world.Scale.Z / frame.Scale.Z : world.Scale.Z);
    SetTransform(entity, local);
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
    TakenIds_.clear();
    BrushMeshes.Clear();
    HiddenEntities.clear();
    LockedEntities.clear();
}

void EditorScene::SyncFromRegistry()
{
    World& world = Registry_.Components;
    Entities = world.GetAliveEntities();

    // The registry changed underneath the scene (a load, or the rollback of a
    // failed one), so the index is rebuilt from what the entities actually
    // carry. Malformed identity is not this function's business: a load reports
    // it through ValidateIdentities and fails.
    TakenIds_.clear();
    TakenIds_.reserve(Entities.size());
    for (EntityId entity : Entities)
        if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
            if (id->Id.IsValid())
                TakenIds_.insert(id->Id.Value);
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

const Transform3f* EditorScene::TryGetLocalTransform(EntityId entity) const
{
    const World& world = Registry_.Components;
    const LocalTransform* local = world.TryGet<LocalTransform>(entity);
    return local != nullptr ? &local->Value : nullptr;
}

const Transform3f* EditorScene::TryGetWorldTransform(EntityId entity) const
{
    const World& world = Registry_.Components;
    if (const WorldTransform* derived = world.TryGet<WorldTransform>(entity))
        return &derived->Value;
    return TryGetLocalTransform(entity);
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

const BakedBrushComponent* EditorScene::TryGetBakedBrush(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<BakedBrushComponent>(entity);
}

const BrushMesh* EditorScene::TryGetDormantBrushMesh(EntityId entity) const
{
    const BakedBrushComponent* baked = TryGetBakedBrush(entity);
    return baked != nullptr ? BrushMeshes.Find(baked->Source) : nullptr;
}

bool EditorScene::IsBrushInstanced(EntityId entity) const
{
    const World& world = Registry_.Components;
    BrushId id{};
    if (const BrushComponent* brush = world.TryGet<BrushComponent>(entity))
        id = brush->Id;
    else if (const BakedBrushComponent* baked = world.TryGet<BakedBrushComponent>(entity))
        id = baked->Source;
    else
        return false;

    for (EntityId other : Entities)
    {
        if (other == entity)
            continue;
        if (const BrushComponent* b = world.TryGet<BrushComponent>(other); b != nullptr && b->Id == id)
            return true;
        if (const BakedBrushComponent* bb = world.TryGet<BakedBrushComponent>(other);
            bb != nullptr && bb->Source == id)
            return true;
    }
    return false;
}

const CameraComponent* EditorScene::TryGetCamera(EntityId entity) const
{
    const World& world = Registry_.Components;
    return world.TryGet<CameraComponent>(entity);
}

std::optional<Aabb3d> EditorScene::TryGetWorldBounds(EntityId entity) const
{
    const BrushMesh* mesh = TryGetBrushMesh(entity);
    if (mesh == nullptr)
        mesh = TryGetDormantBrushMesh(entity); // a baked brush keeps its shape
    const Transform3f* transform = TryGetWorldTransform(entity);
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
