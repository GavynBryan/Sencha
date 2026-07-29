#include "AffordanceRenderer.h"

#include "authoring/EditorComponentAdapter.h"

void AffordanceRenderer::DrawViewport(const FrameContext& frame,
                                      const EditorViewport& viewport)
{
    ViewportAffordanceOutput output;
    Affordances.Build(output);
    if (!output.FillTriangles.empty())
        Fills.Submit(frame, viewport, output.FillTriangles, /*onTop*/ true);
    if (!output.Lines.empty())
        Lines.Submit(frame, viewport, output.Lines, /*onTop*/ true);
}
