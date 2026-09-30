#pragma once

#include "EditorFillPipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class EditorScene;

// Flat alpha-blended fills over brush geometry (the face-highlight pipeline):
// depth-tested so walls occlude them, never depth-writing so a fill cannot
// hide geometry. Used in Solid viewports for the context-zone overlay: a faint
// grey wash over a full-bright body so inactive zones read as inert with their
// textures still visible.
class BrushFillRenderer
{
public:
    explicit BrushFillRenderer(EditorFillPipeline& fills);

    void DrawZoneOverlay(const FrameContext& frame, const EditorViewport& viewport,
                         const CameraRenderData& camera,
                         const EditorScene& scene, const Vec4& color);

private:
    EditorFillPipeline& Fills;
};
