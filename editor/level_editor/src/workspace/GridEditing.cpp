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
        const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(vertexRef->Entity);
        if (elements != nullptr && vertexRef->ElementId < elements->Vertices.size())
        {
            grid.Origin = elements->Vertices[vertexRef->ElementId].Position;
            return;
        }
    }

    Aabb3d bounds = Aabb3d::Empty();
    for (const SelectableRef& ref : selection)
    {
        if (!ref.Entity.IsValid())
            continue;
        if (const auto entityBounds = scene.EvaluatedWorldBounds(ref.Entity))
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

    const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(faceRef.Entity);
    if (elements == nullptr || faceRef.ElementId >= elements->Faces.size())
        return;

    const FaceElement& face = elements->Faces[faceRef.ElementId];
    (void)GridFrame::FromFace(face.Center, face.Normal,
                              GridFrame::LongestEdgeDirection(face.Corners), grid);
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
