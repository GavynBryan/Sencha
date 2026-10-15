#pragma once

#include <graphics/vulkan/ColoredVertexPipelines.h>

#include "render/PreviewBuffer.h"
#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

// Draws the active create-drag preview as an on-top construction wireframe.
// Releasing the drag promotes it to a scene brush, which renders through the
// normal body path while its parameters remain editable.
class BrushPreviewRenderer
{
public:
    BrushPreviewRenderer(PreviewBuffer& preview, ColoredVertexPipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
        const CameraRenderData& camera);

private:
    PreviewBuffer& Preview;
    ColoredVertexPipeline& Lines;
};
