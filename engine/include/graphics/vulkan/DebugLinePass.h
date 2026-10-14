#pragma once

#include <graphics/vulkan/ColoredVertexPipelines.h>
#include <render/extract/DebugLineExtraction.h>

// Draws a DebugLineFrame over the whole target: the depth-tested lines against
// the scene's depth, then the rest over everything. Writes no depth.
class DebugLinePass
{
public:
    void Setup(const RendererServices& services);
    void Draw(const FrameContext& frame, const CameraRenderData& camera, const DebugLineFrame& lines);
    void Teardown();

private:
    ColoredVertexPipeline Lines;
};
