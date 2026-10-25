#pragma once

#include "EditorComponentAdapter.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Aabb3d.h>

#include <functional>
#include <memory>
#include <optional>

class EditorDocument;

// What an entity's mesh spans in the entity's own frame, before its scale: what
// a collider is fitted to. Nullopt when the entity has no mesh, or no mesh has
// loaded to read.
using ColliderMeshBounds = std::function<std::optional<Aabb3d>(const EditorDocument&, EntityId)>;

// The StaticMesh on the entity, read from the document's meshes.
[[nodiscard]] std::optional<Aabb3d> StaticMeshLocalBounds(const EditorDocument& document, EntityId entity);

// A collider drawn where the body will hold it, its box resized by handles, its
// rows the ones its shape has, and fitted to its entity's mesh when added.
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter>
MakeColliderEditorAdapter(ColliderMeshBounds meshBounds = StaticMeshLocalBounds);
