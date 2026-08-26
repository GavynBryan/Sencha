#pragma once

#include "EditorLinePipeline.h"
#include "EditorWideLinePipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class EditorScene;
class SelectionService;

// Draws every irradiance probe volume as a wire box at its authored bounds,
// and for selected volumes the actual lattice points the bake will place
// (derived exactly as the cook derives them, so overhang past the authored
// box is visible). Gathers segments only; owns no GPU state.
class IrradianceVolumeRenderer
{
public:
    IrradianceVolumeRenderer(SelectionService& selection, EditorWideLinePipeline& wideLines,
                             EditorLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera,
                      const EditorScene& scene);

private:
    SelectionService& Selection;
    EditorWideLinePipeline& WideLines;
    EditorLinePipeline& Lines;
};
