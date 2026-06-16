#include "SelectionPivot.h"

#include "../../meshedit/MeshElements.h"
#include "../LevelScene.h"

namespace
{
struct Accumulator
{
    Vec3d Sum = {};
    int Count = 0;

    void Add(Vec3d value)
    {
        Sum += value;
        ++Count;
    }

    [[nodiscard]] std::optional<Vec3d> Average() const
    {
        if (Count == 0)
            return std::nullopt;
        return Sum * (1.0f / static_cast<float>(Count));
    }
};
}

std::optional<Vec3d> ComputeSelectionPivot(const LevelScene& scene,
                                           const SelectionSnapshot& selection,
                                           MeshElementKind kind)
{
    Accumulator pivot;

    for (SelectableRef ref : selection.Items)
    {
        if (!ref.IsValid() || ref.Registry != scene.GetRegistry().Id)
            continue;

        if (kind == MeshElementKind::Object)
        {
            if (!ref.IsEntity())
                continue;
            if (const Transform3f* transform = scene.TryGetTransform(ref.Entity))
                pivot.Add(transform->Position);
            continue;
        }

        const BrushMesh* mesh = scene.TryGetBrushMesh(ref.Entity);
        const Transform3f* transform = scene.TryGetTransform(ref.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        switch (kind)
        {
        case MeshElementKind::Vertex:
            if (ref.IsVertex())
            {
                if (const auto vertex = MeshElements::TryGetVertex(*mesh, *transform, ref.ElementId))
                    pivot.Add(vertex->Position);
            }
            break;
        case MeshElementKind::Edge:
            if (ref.IsEdge())
            {
                if (const auto edge = MeshElements::TryGetEdge(*mesh, *transform, ref.ElementId))
                    pivot.Add(edge->Mid);
            }
            break;
        case MeshElementKind::Face:
            if (ref.IsFace())
            {
                if (const auto face = MeshElements::TryGetFace(*mesh, *transform, ref.ElementId))
                    pivot.Add(face->Center);
            }
            break;
        case MeshElementKind::Object:
        default:
            break;
        }
    }

    return pivot.Average();
}
