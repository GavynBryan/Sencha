#include "SelectionPivot.h"

#include "../meshedit/ElementGeometry.h"
#include "../meshedit/ManipulationSink.h"

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
                                           MeshElementKind kind,
                                           const PivotState& pivotState)
{
    // The transient pivot, once moved, wins over the computed center.
    if (pivotState.Override.has_value())
        return pivotState.Override;

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

        // Each mesh-element ref reports its own center (vertex pos / edge mid /
        // face center); no per-mode switch.
        if (const std::optional<Vec3d> center =
                ElementCenter(*resolved->Mesh, resolved->Transform, ref))
            pivot.Add(*center);
    }

    return pivot.Average();
}
