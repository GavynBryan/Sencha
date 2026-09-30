#pragma once

#include "MeshElements.h"

#include "commands/ICommand.h"
#include "brush/BrushMesh.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <memory>
#include <optional>

struct MeshEditTargetMesh
{
    const BrushMesh* Mesh = nullptr;
    Transform3f Transform = Transform3f::Identity();
    // The scene's retained world elements for this mesh, when the target
    // resolved the scene's own (unedited) mesh; null for a pending edit's
    // working copy, whose elements a caller must derive from Mesh.
    const SourceWorldElements* Elements = nullptr;
};

struct IMeshEditTarget
{
    [[nodiscard]] virtual std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const = 0;
    [[nodiscard]] virtual std::unique_ptr<ICommand> MakeEditCommand(EntityId entity,
                                                                    BrushMesh before,
                                                                    BrushMesh after) = 0;
    virtual ~IMeshEditTarget() = default;
};
