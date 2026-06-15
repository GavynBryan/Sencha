#pragma once

#include "brush/BrushId.h"
#include "brush/BrushMesh.h"
#include "brush/BrushMeshStore.h"

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/EntityId.h>
#include <math/MathSchemas.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Camera.h>
#include <world/registry/Registry.h>

#include <span>
#include <string_view>
#include <vector>

// A brush is now an editable polygon mesh; the component holds a stable BrushId
// into the LevelScene's BrushMeshStore (heavy mesh data kept out of the
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
        // mesh geometry itself is serialized by LevelDocument (§5). Persisted via
        // SceneFieldCodec<BrushId>; the inspector renders it as non-editable.
        return std::tuple{
            MakeField("id", &BrushComponent::Id),
        };
    }
};

class LevelScene
{
public:
    explicit LevelScene(Registry& registry);

    EntityId CreateBrush(Vec3d position, Vec3d halfExtents = { 0.5, 0.5, 0.5 });
    // Creates a brush entity from an explicit mesh (e.g. restoring a deleted brush
    // or loading). The mesh is moved into the store and the entity gets its id.
    EntityId CreateBrushFromMesh(const Transform3f& transform, BrushMesh mesh);
    EntityId CreateCamera(Vec3d position);
    void DestroyEntity(EntityId entity);
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
    // that create entities without going through LevelScene (e.g. scene load).
    void SyncFromRegistry();

    [[nodiscard]] bool HasEntity(EntityId entity) const;
    [[nodiscard]] uint32_t GetEntityCount() const;
    [[nodiscard]] std::span<const EntityId> GetAllEntities() const;
    [[nodiscard]] const Transform3f* TryGetTransform(EntityId entity) const;
    [[nodiscard]] const BrushComponent* TryGetBrush(EntityId entity) const;
    [[nodiscard]] const BrushMesh* TryGetBrushMesh(EntityId entity) const;
    [[nodiscard]] const CameraComponent* TryGetCamera(EntityId entity) const;
    [[nodiscard]] Registry& GetRegistry();
    [[nodiscard]] const Registry& GetRegistry() const;

    // The brush mesh store (serialized as a sidecar by LevelDocument).
    [[nodiscard]] BrushMeshStore& GetBrushMeshStore() { return BrushMeshes; }
    [[nodiscard]] const BrushMeshStore& GetBrushMeshStore() const { return BrushMeshes; }

private:
    Registry& Registry_;
    std::vector<EntityId> Entities;
    BrushMeshStore BrushMeshes;
};
