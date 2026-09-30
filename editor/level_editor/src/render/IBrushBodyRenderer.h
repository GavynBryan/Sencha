#pragma once

class EditorScene;
struct FrameContext;
struct EditorViewport;
struct CameraRenderData;

// A strategy for drawing brush bodies in one viewport (wireframe, solid, …).
// The render feature selects one per viewport by the viewport's ViewportShading,
// so the choice is data-driven: neither the strategies nor the dispatch name a
// specific orientation, and a new shading mode is a new strategy + enum value,
// not an edit to the existing renderers. (OCP)
struct IBrushBodyRenderer
{
    // The scene is per-call: renderers hold no document references, so the
    // workspace can swap the edited document without touching the render stack.
    virtual void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                              const CameraRenderData& camera,
                              const EditorScene& scene) = 0;
    virtual ~IBrushBodyRenderer() = default;
};
