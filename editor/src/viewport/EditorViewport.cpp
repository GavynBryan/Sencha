#include "EditorViewport.h"

#include "GridFrame.h"

void EditorViewport::ApplyOrientation(ViewportOrientation orientation)
{
    Orientation = orientation;

    const OrientationTraits& traits = Traits(orientation);
    Camera.ActiveMode = traits.Mode;

    if (!traits.UsesCameraAxis)
        Camera.OrthoAxis = traits.OrthoAxis;

    // Perspective gets the solid checker preview; ortho grid views stay wireframe.
    Shading = traits.Mode == EditorCamera::Mode::Perspective ? ViewportShading::Solid
                                                             : ViewportShading::Wireframe;
}

const OrientationTraits& EditorViewport::GetOrientationTraits() const
{
    return Traits(Orientation);
}

GridPlane EditorViewport::GetGrid(const GridSettings& settings) const
{
    const OrientationTraits& traits = GetOrientationTraits();
    GridPlane plane = traits.UsesCameraAxis ? GridForAxis(Camera.OrthoAxis) : traits.Grid;
    if (traits.Mode == EditorCamera::Mode::Perspective)
    {
        // Perspective takes the workspace grid frame wholesale, so an
        // origin/axis frame set from geometry rotates the working grid.
        plane.Origin = settings.Origin;
        plane.AxisU = settings.AxisU;
        plane.AxisV = settings.AxisV;
    }
    else if (traits.UsesCameraAxis)
    {
        // Free-axis (User) ortho keeps its camera-derived plane; only the frame
        // origin carries over, projected onto the view plane, so snap phases
        // agree with the other views.
        plane.Origin = plane.Project(settings.Origin);
    }
    else
    {
        // Fixed ortho views are GRID-relative: Top looks down the frame normal,
        // Front along the frame's V, and so on. The orientation's world-frame
        // plane axes map through the grid frame (the workspace points the
        // camera down the matching frame axis), so the working grid is always
        // axis-aligned on screen, never diagonal.
        Vec3d u;
        Vec3d n;
        Vec3d v;
        GridFrame::Basis(settings, u, n, v);
        plane.AxisU = GridFrame::MapToFrame(plane.AxisU, u, n, v);
        plane.AxisV = GridFrame::MapToFrame(plane.AxisV, u, n, v);
        plane.Origin = settings.Origin;
    }
    plane.Spacing = settings.Spacing;
    plane.SnapEnabled = settings.SnapEnabled;
    return plane;
}

const char* EditorViewport::GetDisplayLabel() const
{
    return GetOrientationTraits().Label;
}

float EditorViewport::AspectRatio() const
{
    const float width = RegionMax.x - RegionMin.x;
    const float height = RegionMax.y - RegionMin.y;
    if (width <= 0.0f || height <= 0.0f)
        return 1.0f;
    return width / height;
}

CameraRenderData EditorViewport::BuildRenderData() const
{
    return Camera.BuildRenderData(AspectRatio());
}

bool EditorViewport::Contains(ImVec2 point) const
{
    return point.x >= RegionMin.x
        && point.x <= RegionMax.x
        && point.y >= RegionMin.y
        && point.y <= RegionMax.y;
}
