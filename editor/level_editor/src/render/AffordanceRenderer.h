#pragma once

#include "EditorFillPipeline.h"
#include "EditorWideLinePipeline.h"

class EditorAffordanceService;

// Renders the component-adapter output without knowing which component produced
// it. Adapters own semantics; this renderer owns only the shared line/fill path.
class AffordanceRenderer
{
public:
    AffordanceRenderer(EditorAffordanceService& affordances,
                       EditorWideLinePipeline& lines,
                       EditorFillPipeline& fills)
        : Affordances(affordances), Lines(lines), Fills(fills) {}

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
        const CameraRenderData& camera);

private:
    EditorAffordanceService& Affordances;
    EditorWideLinePipeline& Lines;
    EditorFillPipeline& Fills;
};
