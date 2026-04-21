#include "FourWayViewportLayout.h"

FourWayViewportLayout::FourWayViewportLayout()
{
    Viewports[0].Camera.ActiveMode = EditorCamera::Mode::Perspective;
    Viewports[0].ActiveGrid = GridPlanes::XZ();

    Viewports[1].Camera.ActiveMode = EditorCamera::Mode::Orthographic;
    Viewports[1].Camera.OrthoAxis = { 0.0f, 1.0f, 0.0f };
    Viewports[1].ActiveGrid = GridPlanes::XZ();

    Viewports[2].Camera.ActiveMode = EditorCamera::Mode::Orthographic;
    Viewports[2].Camera.OrthoAxis = { 0.0f, 0.0f, -1.0f };
    Viewports[2].ActiveGrid = GridPlanes::XY();

    Viewports[3].Camera.ActiveMode = EditorCamera::Mode::Orthographic;
    Viewports[3].Camera.OrthoAxis = { -1.0f, 0.0f, 0.0f };
    Viewports[3].ActiveGrid = GridPlanes::YZ();
}

std::span<EditorViewport> FourWayViewportLayout::GetViewports()
{
    return Viewports;
}

EditorViewport* FourWayViewportLayout::GetActiveViewport()
{
    if (ActiveIndex < 0 || ActiveIndex >= 4)
        return nullptr;
    return &Viewports[ActiveIndex];
}

void FourWayViewportLayout::OnResize(uint32_t width, uint32_t height)
{
    Width = width;
    Height = height;
}
