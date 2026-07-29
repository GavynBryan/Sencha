#pragma once

#include "EditorWideLinePipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class WorldDocument;
struct WorldViewSettings;

// Draws one wire AABB per manifest zone (loaded or not) through the wide-line
// pipeline: the focus zone in the selection accent, open zones in the standard
// bounds color, header-only zones dimmed. With the streaming preview on, the
// tint switches to live demand state from the pure policy (focus resolved from
// the perspective viewport's camera; sticky across viewports via the view
// settings). Dock affordances are provided through the component adapter seam.
// Gathers segments only; owns no GPU state.
class ZoneBoundsRenderer
{
public:
    explicit ZoneBoundsRenderer(EditorWideLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      WorldDocument& world, WorldViewSettings& view);

private:
    EditorWideLinePipeline& Lines;
};
