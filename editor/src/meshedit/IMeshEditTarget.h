#pragma once

#include "../commands/ICommand.h"
#include "../brush/BrushMesh.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <memory>
#include <optional>

struct MeshEditTargetMesh
{
    const BrushMesh* Mesh = nullptr;
    Transform3f Transform = Transform3f::Identity();
};

struct IMeshEditTarget
{
    [[nodiscard]] virtual std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const = 0;
    [[nodiscard]] virtual std::unique_ptr<ICommand> MakeEditCommand(EntityId entity,
                                                                    BrushMesh before,
                                                                    BrushMesh after) = 0;
    virtual ~IMeshEditTarget() = default;
};
