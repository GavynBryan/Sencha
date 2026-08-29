#pragma once

#include "brush/BrushId.h"
#include "brush/BrushMesh.h"
#include "brush/BrushMeshStore.h"

#include <core/identity/Id.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/EntityId.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/extract/Camera.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>
#include <zone/WorldPartitionIds.h>

#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <type_traits>
#include <cstdint>
#include <unordered_set>
#include <vector>

// A brush is now an editable polygon mesh; the component holds a stable BrushId
// into the EditorScene's BrushMeshStore (heavy mesh data kept out of the
// trivially-copyable component). (03-brush-representation.md §2.2)
struct BrushComponent
{
    BrushId Id;
};

template <>
struct TypeSchema<BrushComponent>
{
    static constexpr std::string_view Name = "brush";

    static auto Fields()
    {
        // The id links the entity to its mesh in the BrushMeshStore sidecar; the
        // mesh geometry itself is serialized by EditorDocument (§5). Persisted via
        // SceneFieldCodec<BrushId>; the inspector renders it as non-editable.
        return std::tuple{
            MakeField("id", &BrushComponent::Id),
        };
    }
};

// The dormant source of a brush baked to a StaticMesh: the entity swapped its
// BrushComponent for a StaticMeshComponent, but its polygon mesh stays in the
// BrushMeshStore under this id so the bake can be reverted (and the entity
// stays pickable through its source shape). Editor-only, like BrushComponent;
// the level cook strips it from the passthrough scene, so it never reaches the
// runtime.
struct BakedBrushComponent
{
    BrushId Source;
};

template <>
struct TypeSchema<BakedBrushComponent>
{
    static constexpr std::string_view Name = "baked_brush";

    static auto Fields()
    {
        return std::tuple{
            MakeField("source", &BakedBrushComponent::Source),
        };
    }
};

class EditorScene
{
public:
    explicit EditorScene(Registry& registry);

    EntityId CreateBrush(Vec3d position, Vec3d halfExtents = { 0.5, 0.5, 0.5 });
    // Creates a brush entity from an explicit mesh (e.g. restoring a deleted brush
    // or loading). The mesh is moved into the store and the entity gets its id.
    EntityId CreateBrushFromMesh(const Transform3f& transform, BrushMesh mesh);
    EntityId CreateCamera(Vec3d position);
    // A plain entity: just a LocalTransform, ready for game components added via
    // the inspector. The non-brush authoring path (the cook passes such entities
    // through unchanged).
    EntityId CreateEntity(Vec3d position);
    void DestroyEntity(EntityId entity);
    // Adopts an externally-created entity into the tracked list (used to restore
    // a deleted entity, which the registry recreates under a fresh id). Adoption
    // is also where identity is established, so every route into the scene mints
    // or claims exactly once; adopting an already-tracked entity does nothing.
    void TrackEntity(EntityId entity);

    // Replaces the tracked order with `order`, a permutation of the current
    // list (anything else is refused untouched). Sibling rows in the
    // hierarchy follow this order, and local entity records save in it;
    // instance records keep their own order, so a reorder involving a
    // placement's row holds for the session only.
    bool SetEntityOrder(std::span<const EntityId> order);
    void SetTransform(EntityId entity, const Transform3f& transform);
    // Places the entity at a world-space transform, converting through its
    // parent so the stored local transform stays the authored value. Everything
    // that positions an entity from something the user sees -- a gizmo drag, a
    // re-origin, a snap -- works in world space and belongs here; SetTransform
    // is for the local value itself.
    void SetWorldTransform(EntityId entity, const Transform3f& world);

    // The entity's world transform composed from the live local chain, walking
    // its ancestry right now. Mutation paths use this rather than the derived
    // WorldTransform component: the component is refreshed once per frame at
    // the render boundary, and a mutation between refreshes must convert
    // against what the scene IS, not what it last rendered as. Identity for an
    // entity without a local transform.
    [[nodiscard]] Transform3f ComposeWorldTransform(EntityId entity) const;

    // The entity's spatial parent, invalid when it has none.
    [[nodiscard]] EntityId GetParent(EntityId entity) const;
    // Whether `ancestor` appears anywhere on `entity`'s parent chain. False for
    // the entity itself; bounded, so it terminates even over damaged parentage.
    [[nodiscard]] bool IsAncestorOf(EntityId ancestor, EntityId entity) const;
    // Records `parent` as the entity's spatial parent; an invalid parent clears
    // it. The local transform is untouched -- this is the raw relationship, and
    // whether the entity should hold its world position across the change is the
    // caller's decision, made with SetWorldTransform. Refuses (returns false)
    // a dead child, a dead parent, self-parenting, and any parent that is a
    // descendant of the child, so the scene can never hold a cycle.
    bool SetParent(EntityId child, EntityId parent);
    // Appends `root` and every descendant, parents before children. The order
    // is what subtree operations need in both directions: forward for restore
    // and duplication, reverse for leaf-up destruction.
    void CollectSubtree(EntityId root, std::vector<EntityId>& out) const;
    // Destroys `root` and every descendant, leaf-up, so no child ever survives
    // its parent. DestroyEntity on a lone parent instead hands its children to
    // their grandparent; this is for consumers that mean the whole branch.
    void DestroySubtree(EntityId root);
    // Rebuilds the brush's mesh as an axis-aligned box of the given half-extents
    // (the box-editing path; general mesh edits go through BrushOps verbs).
    void SetBrushHalfExtents(EntityId entity, Vec3d halfExtents);
    // Replaces the brush's stored mesh wholesale (used by mesh-edit verbs).
    void SetBrushMesh(EntityId entity, BrushMesh mesh);

    // Overwrites an existing component wholesale. Used by editor commands;
    // does nothing if the entity lacks the component.
    template <typename T>
    void SetComponent(EntityId entity, const T& value)
    {
        static_assert(!std::is_same_v<T, PersistentIdComponent>,
                      "Persistent identity changes go through EnsurePersistentId; "
                      "a wholesale write would desynchronize the scene's id index.");
        if (T* existing = Registry_.Components.TryGet<T>(entity))
            *existing = value;
    }

    // Recomputes every entity's WorldTransform from its LocalTransform and
    // parentage. The document has no single "mutation finished" point -- panels,
    // tools, and commands all write transforms at different points in the frame
    // -- so this is called once at the render extraction boundary, which is
    // after everything that can move an entity this frame and before anything
    // that reads where it ended up.
    void RefreshDerivedTransforms();

    // Destroys every entity in the scene.
    void Clear();

    // Rebuilds the entity list from the registry. Required after operations
    // that create entities without going through EditorScene (e.g. scene load).
    void SyncFromRegistry();

    // Mints a persistent entity id unused by any tracked entity. Editor-side by
    // design (the engine mints no random ids); bit 63 stays clear, reserved for
    // the runtime allocator namespace. The returned id is reserved immediately,
    // so a discarded mint is retired for the life of the document.
    [[nodiscard]] PersistentEntityId MintPersistentId();
    // Establishes the document identity invariant on one entity: it keeps the
    // id it carries unless that id is unset, in the runtime namespace, or
    // already held by another tracked entity (a duplicate or copy-paste of a
    // live source), in which case it is minted fresh. Returns true when a mint
    // happened. Expects an entity not yet contributing to the id index, which is
    // why adoption calls it before the entity counts as tracked.
    bool EnsurePersistentId(EntityId entity);
    // Checks the document invariant a load must arrive at already satisfying:
    // every tracked entity carries a unique authored id. Reading a file is not
    // authoring, so a violation is reported rather than repaired — repairing on
    // load would rewrite the file's identities behind the user and hand the cook
    // ids the source never recorded.
    [[nodiscard]] bool ValidateIdentities(std::string* error) const;

    [[nodiscard]] bool HasEntity(EntityId entity) const;
    [[nodiscard]] uint32_t GetEntityCount() const;
    [[nodiscard]] std::span<const EntityId> GetAllEntities() const;
    // The authored local transform: what the inspector edits and what the scene
    // serializes. Correct answer only for a caller that also writes back a local
    // value, or that works purely in the entity's own frame.
    [[nodiscard]] const Transform3f* TryGetLocalTransform(EntityId entity) const;
    // Where the entity actually is. Every spatial consumer -- rendering,
    // picking, bounds, gizmo placement, geometry rebasing -- wants this one.
    // Falls back to the local transform for an entity that has not yet reached
    // a RefreshDerivedTransforms, which is the right answer for the unparented
    // entity such a read can only be about.
    [[nodiscard]] const Transform3f* TryGetWorldTransform(EntityId entity) const;
    [[nodiscard]] const BrushComponent* TryGetBrush(EntityId entity) const;
    [[nodiscard]] const BrushMesh* TryGetBrushMesh(EntityId entity) const;
    // The dormant source mesh of a baked brush (see BakedBrushComponent).
    // Deliberately separate from TryGetBrushMesh: the mesh-edit paths must not
    // treat a baked entity as editable brush geometry; picking and bounds use
    // this to keep the entity clickable through its source shape.
    [[nodiscard]] const BakedBrushComponent* TryGetBakedBrush(EntityId entity) const;
    [[nodiscard]] const BrushMesh* TryGetDormantBrushMesh(EntityId entity) const;
    // True when another entity shares this entity's brush mesh (live or dormant):
    // the entity is one placement of an instance group.
    [[nodiscard]] bool IsBrushInstanced(EntityId entity) const;
    [[nodiscard]] const CameraComponent* TryGetCamera(EntityId entity) const;
    // World AABB of a brush entity (offset-aware): nullopt when it has no brush
    // mesh/transform or the mesh is empty. Shared by the selection box, the
    // bounds gizmo, and create-from-selection.
    [[nodiscard]] std::optional<Aabb3d> TryGetWorldBounds(EntityId entity) const;
    [[nodiscard]] Registry& GetRegistry();
    [[nodiscard]] const Registry& GetRegistry() const;

    // Per-entity editor view flags (visibility / lock). These are editor-only
    // annotations — NOT ECS components — so they never enter the registry the
    // game module sees, nor serialize as gameplay data. Stored sparsely by slot
    // index (default: visible + unlocked) and cleared when the slot is destroyed
    // so a reused index can't inherit a stale flag. Hidden entities are skipped by
    // the renderers and picking; locked entities are skipped by picking.
    // The entity's own view flags: what its eye and lock toggles show and what
    // the document persists. Rendering and picking use the Effectively variants
    // instead -- a flag inherits down the hierarchy without overwriting any
    // child's own state, so unhiding a parent restores exactly what each child
    // had.
    [[nodiscard]] bool IsEntityVisible(EntityId entity) const;
    [[nodiscard]] bool IsEntityLocked(EntityId entity) const;
    void SetEntityVisible(EntityId entity, bool visible);
    void SetEntityLocked(EntityId entity, bool locked);
    // Own flag AND every ancestor's: what the viewport actually shows and what
    // picking may actually hit.
    [[nodiscard]] bool IsEntityEffectivelyVisible(EntityId entity) const;
    [[nodiscard]] bool IsEntityEffectivelyLocked(EntityId entity) const;

    // The brush mesh store (serialized as a sidecar by EditorDocument).
    [[nodiscard]] BrushMeshStore& GetBrushMeshStore() { return BrushMeshes; }
    [[nodiscard]] const BrushMeshStore& GetBrushMeshStore() const { return BrushMeshes; }

private:
    Registry& Registry_;
    std::vector<EntityId> Entities;
    BrushMeshStore BrushMeshes;
    // Persistent-id minting entropy (per document, like WorldDocument's Rng_).
    std::mt19937_64 IdRng_{ std::random_device{}() };
    // Every persistent id spoken for in this document: held by a tracked entity
    // or reserved by a mint. Minting and duplicate detection are lookups against
    // it rather than scans of the entity list, which is what keeps a bulk paste
    // linear. Rebuilt by SyncFromRegistry; released by DestroyEntity so undo can
    // restore an id.
    std::unordered_set<uint64_t> TakenIds_;
    // Sparse editor view flags keyed by slot index (membership = non-default).
    // Editor view flags, keyed by the entity's persistent id rather than its
    // slot index: slots recycle across projection rebuilds and undo restores,
    // and an index key would silently hand one entity's hide/lock to whatever
    // occupies the slot next. The persistent id cannot alias. A key of zero
    // (an unidentified scratch entity, or a stale handle whose slot was
    // reused) is never stored and always reads visible + unlocked.
    [[nodiscard]] std::uint64_t FlagKeyOf(EntityId entity) const;
    std::unordered_set<std::uint64_t> HiddenEntities;
    std::unordered_set<std::uint64_t> LockedEntities;
};
