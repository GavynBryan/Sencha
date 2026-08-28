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
    void SetTransform(EntityId entity, const Transform3f& transform);
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
    [[nodiscard]] const Transform3f* TryGetTransform(EntityId entity) const;
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
    [[nodiscard]] bool IsEntityVisible(EntityId entity) const;
    [[nodiscard]] bool IsEntityLocked(EntityId entity) const;
    void SetEntityVisible(EntityId entity, bool visible);
    void SetEntityLocked(EntityId entity, bool locked);

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
    std::unordered_set<EntityIndex> HiddenEntities;
    std::unordered_set<EntityIndex> LockedEntities;
};
