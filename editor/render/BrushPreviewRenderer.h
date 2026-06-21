#pragma once

#include "EditorLinePipeline.h"

#include "../level/BrushGeometry.h"
#include "../render/PreviewBuffer.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

// Draws the create-drag preview box via the shared editor line pipeline. It is an
// overlay, not a brush body, so it runs in every viewport regardless of shading.
// That is why the preview is visible in perspective as well as ortho. Owns no GPU
// state of its own.
class BrushPreviewRenderer
{
public:
    BrushPreviewRenderer(PreviewBuffer& preview, EditorLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);

private:
    PreviewBuffer& Preview;
    EditorLinePipeline& Lines;
};
