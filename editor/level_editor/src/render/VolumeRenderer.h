#pragma once

#include "viewport/EditorViewport.h"

#include "EditorLinePipeline.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

class BrushDrawSet;
class EditorInstancedFillPipeline;
class EditorInstancedLinePipeline;

// Volume brushes from the retained draw set: a translucent fill that walls
// occlude and that hides nothing behind it, and their edges. Volumes are air,
// so this is the only way they are drawn.
class VolumeRenderer
{
public:
    VolumeRenderer(EditorInstancedFillPipeline& fill, EditorInstancedLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera, const BrushDrawSet& draws);

private:
    EditorInstancedFillPipeline& Fill;
    EditorInstancedLinePipeline& Lines;
    std::vector<ColoredVertex> Scratch;
};
