#include "SelectionPivot.h"

#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshElements.h"

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

std::optional<Vec3d> ComputeSelectionPivot(const ManipulationSink& sink,
                                           const SelectionSnapshot& selection,
                                           MeshElementKind kind)
{
    Accumulator pivot;

    for (SelectableRef ref : selection.Items)
    {
        if (!ref.IsValid())
            continue;

        if (kind == MeshElementKind::Object)
        {
            if (ref.IsEntity())
            {
                if (const std::optional<Transform3f> transform = sink.ResolveTransform(ref.Entity))
                    pivot.Add(transform->Position);
            }
            continue;
        }

        const std::optional<MeshEditTargetMesh> resolved = sink.ResolveMesh(ref.Entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            continue;
        const BrushMesh& mesh = *resolved->Mesh;
        const Transform3f& transform = resolved->Transform;

        switch (kind)
        {
        case MeshElementKind::Vertex:
            if (ref.IsVertex())
            {
                if (const auto vertex = MeshElements::TryGetVertex(mesh, transform, ref.ElementId))
                    pivot.Add(vertex->Position);
            }
            break;
        case MeshElementKind::Edge:
            if (ref.IsEdge())
            {
                if (const auto edge = MeshElements::TryGetEdge(mesh, transform, ref.ElementId))
                    pivot.Add(edge->Mid);
            }
            break;
        case MeshElementKind::Face:
            if (ref.IsFace())
            {
                if (const auto face = MeshElements::TryGetFace(mesh, transform, ref.ElementId))
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
