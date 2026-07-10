#pragma once

#include "EditorImmediatePipeline.h"

#include <math/Vec.h>

#include <span>
#include <string_view>

// One world-space line segment with a screen-space pixel width. Unlike the 1px
// LINE_LIST EditorLinePipeline, the wide-line pipeline expands each segment into a
// camera-facing quad, so strokes have an exact pixel width and analytic AA.
// Also the GPU instance record: the vertex shader expands each segment into six
// vertices, so the upload is one of these per segment (no CPU expansion).
struct EditorLineSegment
{
    Vec3d A;
    Vec3d B;
    Vec4  Color;
    float WidthPx = 1.0f; // full stroke width in pixels
};

// Screen-space expanded line pipeline: an instanced TRIANGLE_LIST, alpha-blended
// configuration of the shared EditorImmediatePipeline. Producers gather
// EditorLineSegment lists and call Submit; each segment is one instance and the
// vertex shader builds the ribbon quad. Oversized submissions are split into
// scratch-sized chunks rather than dropped.
class EditorWideLinePipeline
{
public:
    void Setup(const RendererServices& services);
    // onTop = true draws without depth-testing (overlay visible through geometry);
    // false depth-tests against the scene.
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorLineSegment> segments,
                bool onTop = false,
                std::string_view source = {});
    void Teardown();

private:
    EditorImmediatePipeline<EditorLineSegment> Pipeline;
};
