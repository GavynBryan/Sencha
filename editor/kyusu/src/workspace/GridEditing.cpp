#include "GridEditing.h"

#include "document/EditorScene.h"
#include "meshedit/MeshElements.h"
#include "viewport/EditorCamera.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridFrame.h"
#include "viewport/GridSettings.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportOrientation.h"

#include <math/geometry/3d/Aabb3d.h>

#include <cmath>

void GridEditing::SetOriginToSelection(GridSettings& grid, const EditorScene& scene,
                                       std::span<const SelectableRef> selection)
{
    // A single selected vertex is the exact intent; use its world position.
    const SelectableRef* vertexRef = nullptr;
    for (const SelectableRef& ref : selection)
    {
        if (!ref.IsVertex())
            continue;
        if (vertexRef != nullptr)
        {
            vertexRef = nullptr;
            break;
        }
        vertexRef = &ref;
    }
    if (vertexRef != nullptr)
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(vertexRef->Entity);
        const Transform3f* transform = scene.TryGetTransform(vertexRef->Entity);
        if (mesh != nullptr && transform != nullptr)
        {
            if (const auto vertex = MeshElements::TryGetVertex(*mesh, *transform, vertexRef->ElementId))
            {
                grid.Origin = vertex->Position;
                return;
            }
        }
    }

    Aabb3d bounds = Aabb3d::Empty();
    for (const SelectableRef& ref : selection)
    {
        if (!ref.Entity.IsValid())
            continue;
        if (const auto entityBounds = scene.TryGetWorldBounds(ref.Entity))
            bounds.ExpandToInclude(*entityBounds);
    }
    if (bounds.IsValid())
        grid.Origin = bounds.Center();
}

void GridEditing::AlignToSelectedFace(GridSettings& grid, const EditorScene& scene,
                                      std::span<const SelectableRef> selection, SelectableRef primary)
{
    SelectableRef faceRef = primary;
    if (!faceRef.IsFace())
    {
        faceRef = {};
        for (const SelectableRef& ref : selection)
            if (ref.IsFace())
            {
                faceRef = ref;
                break;
            }
    }
    if (!faceRef.IsFace())
        return;

    const BrushMesh* mesh = scene.TryGetBrushMesh(faceRef.Entity);
    const Transform3f* transform = scene.TryGetTransform(faceRef.Entity);
    if (mesh == nullptr || transform == nullptr)
        return;

    const auto face = MeshElements::TryGetFace(*mesh, *transform, faceRef.ElementId);
    if (!face.has_value())
        return;

    (void)GridFrame::FromFace(face->Center, face->Normal,
                              GridFrame::LongestEdgeDirection(face->Corners), grid);
}

void GridEditing::SyncOrthoViews(const GridSettings& grid, ViewportLayout& layout)
{
    Vec3d u;
    Vec3d n;
    Vec3d v;
    GridFrame::Basis(grid, u, n, v);

    for (const auto& viewport : layout.All())
    {
        const OrientationTraits& traits = viewport->GetOrientationTraits();
        if (traits.Mode != EditorCamera::Mode::Orthographic || traits.UsesCameraAxis)
            continue;

        viewport->Camera.OrthoAxis = GridFrame::MapToFrame(traits.OrthoAxis, u, n, v);
        // The same view-up rule the world-aligned basis uses (world up, or
        // forward when looking straight down/up), expressed in the frame.
        const Vec3d upDefault = std::abs(traits.OrthoAxis.Y) > 0.999f ? Vec3d::Forward() : Vec3d::Up();
        viewport->Camera.OrthoUpHint = GridFrame::MapToFrame(upDefault, u, n, v);
    }
}
