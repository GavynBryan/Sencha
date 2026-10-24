#pragma once

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/ColoredVertexPipelines.h>

#include <graphics/vulkan/Renderer.h>

#include <vector>

class BrushDrawSet;
class EditorInstancedFillPipeline;
class EditorInstancedLinePipeline;
struct Registry;

// Volume brushes from the retained draw set: a translucent fill that walls
// occlude and that hides nothing behind it, and their edges. Volumes are air,
// so this is the only way they are drawn. A volume whose entity carries a
// component declaring a volume tint (EditorVisual) is drawn in that tint;
// `registry` is where the draw set's entities live, or null for none.
class VolumeRenderer
{
public:
    VolumeRenderer(EditorInstancedFillPipeline& fill, EditorInstancedLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera, const BrushDrawSet& draws,
                      const Registry* registry = nullptr);

private:
    EditorInstancedFillPipeline& Fill;
    EditorInstancedLinePipeline& Lines;
    std::vector<ColoredVertex> Scratch;
};
