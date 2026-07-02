#pragma once

struct FrameContext;
struct EditorViewport;

// A strategy for drawing brush bodies in one viewport (wireframe, solid, …).
// The render feature selects one per viewport by the viewport's ViewportShading,
// so the choice is data-driven: neither the strategies nor the dispatch name a
// specific orientation, and a new shading mode is a new strategy + enum value,
// not an edit to the existing renderers. (OCP)
struct IBrushBodyRenderer
{
    virtual void DrawViewport(const FrameContext& frame, const EditorViewport& viewport) = 0;
    virtual ~IBrushBodyRenderer() = default;
};
