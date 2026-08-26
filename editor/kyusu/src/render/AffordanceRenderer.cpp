#include "AffordanceRenderer.h"

#include "authoring/EditorComponentAdapter.h"

void AffordanceRenderer::DrawViewport(const FrameContext& frame,
                                      const EditorViewport& viewport,
                                      const CameraRenderData& camera)
{
    ViewportAffordanceOutput output;
    Affordances.Build(output);
    if (!output.FillTriangles.empty())
        Fills.Submit(frame, viewport, camera, output.FillTriangles, /*onTop*/ true);
    if (!output.Lines.empty())
        Lines.Submit(frame, viewport, camera, output.Lines, /*onTop*/ true);
}
