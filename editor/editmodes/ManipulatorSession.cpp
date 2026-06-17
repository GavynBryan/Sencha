#include "ManipulatorSession.h"

#include "BoundsManipulator.h"
#include "TranslateManipulator.h"
#include "../interaction/InteractionHost.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"

#include <imgui.h>

#include <utility>

ManipulatorSession::ManipulatorSession(SelectionService& selection,
                                       MeshEditService& service,
                                       ManipulationSink& sink)
    : Selection(selection)
    , Service(service)
    , Sink(sink)
{
    // The only registration site: new manipulators (rotate/scale/clip) land here
    // and nowhere else. Order is priority — bounds handles take a hit before the
    // translate gizmo in the ortho views where both apply.
    Manipulators.push_back(std::make_unique<BoundsManipulator>());
    Manipulators.push_back(std::make_unique<TranslateManipulator>());
}

InputConsumed ManipulatorSession::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const ImVec2 pos = pointer.Position;
    const SelectionSnapshot snapshot = Selection.GetSnapshot();
    const ManipulatorContext mctx{ snapshot, Service, Sink };

    for (const std::unique_ptr<IManipulator>& manipulator : Manipulators)
    {
        if (!manipulator->AppliesTo(mctx, viewport))
            continue;
        const int part = manipulator->HitTest(mctx, viewport, pos);
        if (part == 0)
            continue;
        if (auto interaction = manipulator->BeginDrag(part, mctx, viewport, pos))
        {
            ctx.Interactions.Begin(ctx, std::move(interaction));
            return InputConsumed::Yes;
        }
    }

    return InputConsumed::No;
}

void ManipulatorSession::BuildVisuals(const EditorViewport& viewport, ManipulatorVisual& out) const
{
    const SelectionSnapshot snapshot = Selection.GetSnapshot();
    const ManipulatorContext mctx{ snapshot, Service, Sink };

    // Hover: when the cursor is over this viewport, find the part it would grab —
    // in the same priority order routing uses, so only the manipulator that would
    // receive the click shows a hovered part.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inViewport = mouse.x >= viewport.RegionMin.x && mouse.x <= viewport.RegionMax.x
                         && mouse.y >= viewport.RegionMin.y && mouse.y <= viewport.RegionMax.y;
    int hoveredIndex = -1;
    int hoveredPart = 0;
    if (inViewport)
    {
        for (std::size_t i = 0; i < Manipulators.size(); ++i)
        {
            if (!Manipulators[i]->AppliesTo(mctx, viewport))
                continue;
            if (const int part = Manipulators[i]->HitTest(mctx, viewport, mouse))
            {
                hoveredIndex = static_cast<int>(i);
                hoveredPart = part;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < Manipulators.size(); ++i)
    {
        if (!Manipulators[i]->AppliesTo(mctx, viewport))
            continue;
        Manipulators[i]->BuildVisual(mctx, viewport,
                                     static_cast<int>(i) == hoveredIndex ? hoveredPart : 0, out);
    }
}
