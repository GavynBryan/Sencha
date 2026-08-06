#include "EditorScene.h"

#include "brush/BrushBounds.h"
#include "brush/BrushOps.h"

#include <world/identity/PersistentIdComponent.h>
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
    World& world = Registry_.Components;
    std::unordered_set<uint64_t> taken;
    taken.reserve(Entities.size());
    for (EntityId entity : Entities)
        if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
            taken.insert(id->Id.Value);
    return MintFromTaken(taken);
}

PersistentEntityId EditorScene::MintFromTaken(std::unordered_set<uint64_t>& taken)
{
    while (true)
    {
        const uint64_t value = IdRng_() & ~PersistentEntityIdRuntimeBit;
        if (value != 0 && taken.insert(value).second)
            return PersistentEntityId{ value };
    }
}

bool EditorScene::EnsurePersistentId(EntityId entity)
{
    World& world = Registry_.Components;
    PersistentIdComponent* id = world.TryGet<PersistentIdComponent>(entity);

    bool duplicate = false;
    if (id != nullptr && id->Id.IsValid())
    {
        for (EntityId other : Entities)
        {
            if (other == entity)
                continue;
            const auto* otherId = world.TryGet<PersistentIdComponent>(other);
            if (otherId != nullptr && otherId->Id == id->Id)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            return false;
    }

    const PersistentEntityId minted = MintPersistentId();
    if (id != nullptr)
        id->Id = minted;
    else
        world.AddComponent(entity, PersistentIdComponent{ minted });
    return true;
}

size_t EditorScene::BackfillPersistentIds()
{
    World& world = Registry_.Components;
    std::unordered_set<uint64_t> taken;
    taken.reserve(Entities.size());

    // First pass claims every valid id; the first holder keeps it. The second
    // pass mints for whoever is left (missing, unset, or a losing duplicate),
    // sharing one taken-set so a legacy document migrates in linear time.
    std::vector<EntityId> needsMint;
    for (EntityId entity : Entities)
    {
        const auto* id = world.TryGet<PersistentIdComponent>(entity);
        if (id == nullptr || !id->Id.IsValid() || !taken.insert(id->Id.Value).second)
            needsMint.push_back(entity);
    }

    for (EntityId entity : needsMint)
    {
        const PersistentEntityId fresh = MintFromTaken(taken);
        if (auto* id = world.TryGet<PersistentIdComponent>(entity))
            id->Id = fresh;
        else
            world.AddComponent(entity, PersistentIdComponent{ fresh });
    }
    return needsMint.size();
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
