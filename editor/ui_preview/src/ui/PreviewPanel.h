#pragma once

#include "PreviewViewState.h"
#include "authoring/UiPreviewSession.h"

#include "render/UiSurfaceTargetRenderFeature.h"
#include "ui/IEditorPanel.h"

#include <math/geometry/2d/Rect2d.h>

#include <optional>

//=============================================================================
// PreviewPanel
//
// The document, drawn by the engine's own pass into the session's target and
// fitted into the panel. Owns nothing but its own zoom arithmetic: what the
// pointer does over the image is the session's policy (Interact delivers,
// Inspect measures), and where the image landed this frame is told to the
// session as the surface's placement so the layer maps events the same way
// this panel maps its inspection hover.
//=============================================================================
class PreviewPanel final : public IEditorPanel
{
public:
    PreviewPanel(UiPreviewSession& session,
                 PreviewViewState& view,
                 UiSurfaceTargetRenderFeature& target,
                 UiSurfaceTargetId binding);

    [[nodiscard]] std::string_view GetTitle() const override { return "Preview"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "preview", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    void DrawControls();
    void DrawResolutionControls();
    void DrawScaleControls();
    void DrawZoomControls();
    void DrawNavigation();
    void DrawImage();
    void DrawInspection(const Rect2d& placement);
    void DrawSafeArea(const Rect2d& placement);
    void DrawOutline(const Rect2d& placement, UiElementRef ref, ImU32 color, float thickness);

    UiPreviewSession& Session;
    PreviewViewState& View;
    UiSurfaceTargetRenderFeature& Target;
    UiSurfaceTargetId Binding;

    int CustomWidth = 1920;
    int CustomHeight = 1080;
    float CustomScale = 1.0f;
};
