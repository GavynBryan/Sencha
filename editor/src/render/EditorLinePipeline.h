#pragma once

#include "EditorImmediatePipeline.h"

#include <math/Vec.h>

#include <span>

// One world-space colored-line vertex. The single vertex format every editor
// overlay (brush wireframe, selection highlight, manipulators, component visuals)
// draws with.
struct EditorLineVertex
{
    Vec3d Position;
    Vec4 Color;
};

// The one line pipeline shared by all editor overlay renderers: a LINE_LIST
// configuration of the shared EditorImmediatePipeline. Producers gather
// EditorLineVertex lists and call Submit.
class EditorLinePipeline
{
public:
    void Setup(const RendererServices& services);
    // onTop = true draws without depth-testing, so manipulators/selection are
    // visible through geometry; false depth-tests against the scene.
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorLineVertex> vertices,
                bool onTop = false);
    void Teardown();

private:
    EditorImmediatePipeline<EditorLineVertex> Pipeline;
};
