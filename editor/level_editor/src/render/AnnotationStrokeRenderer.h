#pragma once

#include "EditorWideLinePipeline.h"

#include <vector>

struct EditorOverlayState;

// The world-space strokes tools and drags lay over the views -- construction
// lines, a rotation dial's rim and stops, a drag's from-to line -- drawn into
// each view on top of the scene. What sits at a point rather than along a line
// (labels, handles, buttons, the dial's knob) is the views document's.
class AnnotationStrokeRenderer
{
public:
    AnnotationStrokeRenderer(const EditorOverlayState& overlay, EditorWideLinePipeline& lines)
        : Overlay(overlay), Lines(lines) {}

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport, const CameraRenderData& camera);

private:
    const EditorOverlayState& Overlay;
    EditorWideLinePipeline& Lines;
    std::vector<EditorLineSegment> Strokes;
};
