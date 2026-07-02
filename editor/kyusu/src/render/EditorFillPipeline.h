#pragma once

#include "EditorImmediatePipeline.h"
#include "EditorLinePipeline.h"

#include <span>

// Alpha-blended flat-color triangles for translucent overlay fills (selected-face
// highlight): a TRIANGLE_LIST configuration of the shared EditorImmediatePipeline
// over the same position+color vertex and shaders the line pipeline uses. Never
// writes depth, so a fill cannot occlude; strokes submitted after it read on top.
class EditorFillPipeline
{
public:
    void Setup(const RendererServices& services);
    // onTop = true draws without depth-testing (fill visible through geometry);
    // false depth-tests against the scene.
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorLineVertex> vertices,
                bool onTop = false);
    void Teardown();

private:
    EditorImmediatePipeline<EditorLineVertex> Pipeline;
};
