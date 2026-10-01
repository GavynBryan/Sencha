#pragma once

#include "EditorImmediatePipeline.h"
#include "EditorLineSegment.h"

#include <math/Vec.h>

#include <span>
#include <string_view>

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
                const Rect2d& region,
                const CameraRenderData& camera,
                std::span<const EditorLineSegment> segments,
                bool onTop = false,
                std::string_view source = {});
    void Teardown();

private:
    EditorImmediatePipeline<EditorLineSegment> Pipeline;
};
