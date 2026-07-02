#pragma once

#include "EditorImmediatePipeline.h"

#include <math/Vec.h>

#include <span>

// One world-space solid vertex for the brush preview pass: position + normal for
// simple lighting, UV (from the per-face projection) for the checker, and a
// per-material tint.
struct EditorSolidVertex
{
    Vec3d Position; // world space
    Vec3d Normal;   // world space
    Vec2d Uv;
    Vec4  Tint;
};

// The depth-writing solid pipeline for brush face preview: a TRIANGLE_LIST,
// depth-writing configuration of the shared EditorImmediatePipeline. Opaque, so
// it draws after the grid and before the wireframe overlay.
class EditorSolidPipeline
{
public:
    void Setup(const RendererServices& services);
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorSolidVertex> vertices);
    // Backface-cull the solid pass to match play mode (default on); off draws both sides.
    void SetCullBackfaces(bool on);
    void Teardown();

private:
    EditorImmediatePipeline<EditorSolidVertex> Pipeline;
};
