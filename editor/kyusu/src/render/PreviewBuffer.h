#pragma once

#include "brush/BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>

#include <optional>

// The live create-drag preview: the actual brush mesh that will be committed,
// at the transform it will be placed. The renderer draws its wireframe, so any
// primitive (box, plane, cylinder) previews as its true silhouette with no
// per-shape preview code.
struct PreviewMesh
{
    Transform3f Transform;
    BrushMesh   Mesh;
};

class PreviewBuffer
{
public:
    void SetMesh(const Transform3f& transform, BrushMesh mesh);
    void Clear();
    [[nodiscard]] const std::optional<PreviewMesh>& GetMesh() const;

private:
    std::optional<PreviewMesh> Preview;
};
