#include "ManipulatorSession.h"

#include "BoundsManipulator.h"
#include "TranslateManipulator.h"
#include "../interaction/InteractionHost.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"

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

    for (const std::unique_ptr<IManipulator>& manipulator : Manipulators)
        if (manipulator->AppliesTo(mctx, viewport))
            manipulator->BuildVisual(mctx, viewport, out);
}
