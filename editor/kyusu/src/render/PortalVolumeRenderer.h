#pragma once

#include "EditorFillPipeline.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

class EditorScene;

// Portal marker volumes as alpha-blended flat fills (the face-highlight
// pipeline): translucent from both sides, depth-tested so walls occlude them,
// never depth-writing so a marker cannot hide geometry. The real-material body
// pass rightly skips portals (they are not geometry); Solid viewports draw
// them through here, focus full-strength and context zones dimmed.
class PortalVolumeRenderer
{
public:
    explicit PortalVolumeRenderer(EditorFillPipeline& fills);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const EditorScene& scene, const Vec4& tint);

private:
    EditorFillPipeline& Fills;
};
