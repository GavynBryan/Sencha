#pragma once

#include "EditorWideLinePipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class WorldDocument;

// Draws one wire AABB per manifest zone (loaded or not) through the wide-line
// pipeline: the focus zone in the selection accent, open zones in the standard
// bounds color, header-only zones dimmed. Gathers segments only; owns no GPU
// state.
class ZoneBoundsRenderer
{
public:
    explicit ZoneBoundsRenderer(EditorWideLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      WorldDocument& world);

private:
    EditorWideLinePipeline& Lines;
};
