#pragma once

#include "EditorImmediatePipeline.h"

#include <math/Vec.h>

#include <span>
#include <vector>

// One world-space line segment with a screen-space pixel width. Unlike the 1px
// LINE_LIST EditorLinePipeline, the wide-line pipeline expands each segment into a
// camera-facing quad, so strokes have an exact pixel width and analytic AA.
struct EditorLineSegment
{
    Vec3d A;
    Vec3d B;
    Vec4  Color;
    float WidthPx = 1.0f; // full stroke width in pixels
};

// Per-corner vertex the wide-line shader expands. Carries both endpoints so the
// vertex shader can build the screen-space perpendicular, plus the side and width.
struct EditorWideLineVertex
{
    Vec3d Position;          // this endpoint
    Vec3d Other;             // opposite endpoint
    Vec4  Color;
    float HalfWidthPx = 0.0f;
    float Side = 0.0f;       // -1 or +1
};

// Screen-space expanded line pipeline: a TRIANGLE_LIST, alpha-blended configuration
// of the shared EditorImmediatePipeline. Producers gather EditorLineSegment lists
// and call Submit; the pipeline expands each segment to two triangles on the CPU.
class EditorWideLinePipeline
{
public:
    void Setup(const RendererServices& services);
    // onTop = true draws without depth-testing (overlay visible through geometry);
    // false depth-tests against the scene.
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorLineSegment> segments,
                bool onTop = false);
    void Teardown();

private:
    EditorImmediatePipeline<EditorWideLineVertex> Pipeline;
    std::vector<EditorWideLineVertex> Expanded; // reused per Submit to avoid per-frame allocs
};
