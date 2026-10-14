#pragma once

#include "viewport/EditorViewport.h"
#include <graphics/vulkan/ColoredVertexPipelines.h>

#include <span>

// Alpha-blended flat-color triangles for translucent overlay fills (selected-face
// highlight): a TRIANGLE_LIST configuration of the shared ImmediatePipeline
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
                const CameraRenderData& camera,
                std::span<const ColoredVertex> vertices,
                bool onTop = false);
    void Teardown();

private:
    ColoredVertexPipeline Pipeline;
};
