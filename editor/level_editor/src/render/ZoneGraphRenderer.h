#pragma once

#include "render/EditorWideLinePipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class WorldDocument;

// Draws a world's zone graph over the views: every dock and teleport link as a
// line between the centres of the zones it joins (docks solid, links dashed, a
// one-way connection with an arrowhead where it leads), coloured by what the
// world's validation says about it. Drawn on top, since the author is usually
// inside a zone whose walls would hide it. Gathers segments only.
class ZoneGraphRenderer
{
public:
    explicit ZoneGraphRenderer(EditorWideLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport, const CameraRenderData& camera,
                      const WorldDocument& world);

private:
    EditorWideLinePipeline& Lines;
};
