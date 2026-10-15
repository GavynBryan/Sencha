#pragma once

#include "viewport/EditorViewport.h"
#include <graphics/vulkan/ColoredVertexPipelines.h>

#include <span>

// LINE_LIST configuration of the shared immediate pipeline with a per-instance
// model matrix: one mesh's edges in its own frame drawn once per placement.
// What the brush wireframe uses so a repeated brush uploads its edges once.
class EditorInstancedLinePipeline
{
public:
    void Setup(const RendererServices& services);
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                const CameraRenderData& camera,
                std::span<const ColoredVertex> vertices,
                std::span<const ColoredVertexInstance> instances,
                bool onTop = false);
    void Teardown();

private:
    ColoredVertexPipeline Pipeline;
};
