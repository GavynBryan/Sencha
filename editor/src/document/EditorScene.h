#pragma once

#include "../brush/BrushId.h"
#include "../brush/BrushMesh.h"
#include "../brush/BrushMeshStore.h"

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/EntityId.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>

#include <optional>
#include <span>
#include <string_view>
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
    // a deleted entity, which the registry recreates under a fresh id).
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
        if (T* existing = Registry_.Components.TryGet<T>(entity))
            *existing = value;
    }

    // Destroys every entity in the scene.
    void Clear();

    // Rebuilds the entity list from the registry. Required after operations
    // that create entities without going through EditorScene (e.g. scene load).
    void SyncFromRegistry();

    [[nodiscard]] bool HasEntity(EntityId entity) const;
    [[nodiscard]] uint32_t GetEntityCount() const;
    [[nodiscard]] std::span<const EntityId> GetAllEntities() const;
    [[nodiscard]] const Transform3f* TryGetTransform(EntityId entity) const;
    [[nodiscard]] const BrushComponent* TryGetBrush(EntityId entity) const;
    [[nodiscard]] const BrushMesh* TryGetBrushMesh(EntityId entity) const;
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
    // Sparse editor view flags keyed by slot index (membership = non-default).
    std::unordered_set<EntityIndex> HiddenEntities;
    std::unordered_set<EntityIndex> LockedEntities;
};
