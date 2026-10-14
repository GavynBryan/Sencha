#pragma once

#include "viewport/EditorViewport.h"
#include <graphics/vulkan/ColoredVertexPipelines.h>

#include <span>

// Model matrices by rows, read in place as the instances they already are.
[[nodiscard]] inline std::span<const ColoredVertexInstance> AsLineInstances(std::span<const Mat4> rows)
{
    static_assert(sizeof(ColoredVertexInstance) == sizeof(Mat4));
    return { reinterpret_cast<const ColoredVertexInstance*>(rows.data()), rows.size() };
}

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
