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
    auto* index = world.TryGetResource<PersistentEntityIndex>();

    if (id != nullptr && IsAuthoredPersistentEntityId(id->Id))
    {
        // A successful insert means the id was genuinely free and the entity
        // keeps it. A failed insert usually means another tracked entity holds
        // the value -- but an id this document minted FOR this entity is
        // reserved in TakenIds_ before the entity exists, so ownership is
        // settled by the index: the component's own registration ran when the
        // component was added, and if the id resolves to this entity, the
        // reservation is this entity's.
        if (TakenIds_.insert(id->Id.Value).second)
            return false;
        if (index != nullptr && index->TryResolve(id->Id) == entity)
            return false;
    }

    const PersistentEntityId minted = MintPersistentId();
    if (id != nullptr)
    {
        // Through the index, or the old registration would keep resolving an
        // id this entity no longer carries.
        if (index != nullptr)
            index->Unregister(id->Id, entity);
        id->Id = minted;
        if (index != nullptr)
            (void)index->Register(minted, entity);
    }
    else
    {
        world.AddComponent(entity, PersistentIdComponent{ minted });
    }
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

Transform3f EditorScene::ComposeWorldTransform(EntityId entity) const
{
    const World& world = Registry_.Components;
    Transform3f composed = Transform3f::Identity();
    if (const LocalTransform* local = world.TryGet<LocalTransform>(entity))
        composed = local->Value;

    // Bounded like IsAncestorOf, so damaged parentage terminates.
    std::size_t remaining = Entities.size();
    for (EntityId parent = GetParent(entity);
         parent.IsValid() && remaining > 0;
         parent = GetParent(parent), --remaining)
    {
        if (const LocalTransform* local = world.TryGet<LocalTransform>(parent))
            composed = local->Value * composed;
    }
    return composed;
}

EntityId EditorScene::GetParent(EntityId entity) const
{
    const Parent* parent = Registry_.Components.TryGet<Parent>(entity);
    return parent != nullptr ? parent->Entity : EntityId{};
}

bool EditorScene::IsAncestorOf(EntityId ancestor, EntityId entity) const
{
    if (!ancestor.IsValid())
        return false;

    // Bounded by the entity count so damaged parentage (a cycle that predates
    // the SetParent guard, or a stale id) walks off the end instead of forever.
    std::size_t remaining = Entities.size();
    for (EntityId current = GetParent(entity);
         current.IsValid() && remaining > 0;
         current = GetParent(current), --remaining)
    {
        if (current == ancestor)
            return true;
    }
    return false;
}

bool EditorScene::SetParent(EntityId child, EntityId parent)
{
    World& world = Registry_.Components;
    if (!world.IsAlive(child))
        return false;

    if (!parent.IsValid())
    {
        if (world.TryGet<Parent>(child) != nullptr)
            world.RemoveComponent<Parent>(child);
        return true;
    }

    if (!world.IsAlive(parent) || parent == child || IsAncestorOf(child, parent))
        return false;

    if (Parent* existing = world.TryGet<Parent>(child))
        existing->Entity = parent;
    else
        world.AddComponent(child, Parent{ parent });
    return true;
}

void EditorScene::CollectSubtree(EntityId root, std::vector<EntityId>& out) const
{
    if (!Registry_.Components.IsAlive(root))
        return;

    // Breadth-first over the tracked list. Quadratic in scene size for a deep
    // tree, but subtree operations are user gestures over editor-scale scenes,
    // not a per-frame path.
    const std::size_t first = out.size();
    out.push_back(root);
    for (std::size_t cursor = first; cursor < out.size(); ++cursor)
        for (EntityId candidate : Entities)
            if (GetParent(candidate) == out[cursor])
                out.push_back(candidate);
}

void EditorScene::DestroySubtree(EntityId root)
{
    std::vector<EntityId> subtree;
    CollectSubtree(root, subtree);
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
        DestroyEntity(*it);
}

void EditorScene::RefreshDerivedTransforms()
{
    World& world = Registry_.Components;
    if (!world.IsRegistered<LocalTransform>() || !world.IsRegistered<WorldTransform>())
        return;

    // Change detection compares column versions against the frame counter, and
    // an edit made in the same frame as the sweep that must observe it is
    // invisible to that comparison (TransformPropagation.cpp spells this out).
    // Each refresh is therefore one frame of the document's world; it is also
    // what moves the sweeps off their frame-zero full-sweep fallback.
    world.AdvanceFrame();

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

    // A destroyed parent hands its children to their grandparent (or the root)
    // at their current world position, so deleting one entity never teleports
    // or strands a branch. Deleting a branch on purpose is DestroySubtree,
    // which reaches here with no children left to adopt.
    const EntityId grandparent = GetParent(entity);
    for (EntityId candidate : Entities)
    {
        if (candidate == entity || GetParent(candidate) != entity)
            continue;
        const Transform3f held = ComposeWorldTransform(candidate);
        (void)SetParent(candidate, grandparent);
        SetWorldTransform(candidate, held);
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
    const EntityId parent = GetParent(entity);
    if (!parent.IsValid())
    {
        SetTransform(entity, world);
        return;
    }

    // The exact inverse of the parent-times-child composition transform
    // propagation applies, so a value placed here reads back unchanged. The
    // frame is composed live rather than read from the derived component,
    // which can be a refresh behind the mutation being made.
    const Transform3f frame = ComposeWorldTransform(parent);
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

bool EditorScene::IsEntityEffectivelyVisible(EntityId entity) const
{
    if (!IsEntityVisible(entity))
        return false;
    std::size_t remaining = Entities.size();
    for (EntityId parent = GetParent(entity);
         parent.IsValid() && remaining > 0;
         parent = GetParent(parent), --remaining)
    {
        if (!IsEntityVisible(parent))
            return false;
    }
    return true;
}

bool EditorScene::IsEntityEffectivelyLocked(EntityId entity) const
{
    if (IsEntityLocked(entity))
        return true;
    std::size_t remaining = Entities.size();
    for (EntityId parent = GetParent(entity);
         parent.IsValid() && remaining > 0;
         parent = GetParent(parent), --remaining)
    {
        if (IsEntityLocked(parent))
            return true;
    }
    return false;
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
