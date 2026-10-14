#include "input/OriginViewportStamp.h"

#include "input/InputRouter.h"
#include "viewport/ViewportLayout.h"

#include <variant>

void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event)
{
    // A gesture pinned to a viewport keeps every event there; any other
    // positioned event belongs to the viewport under the cursor, which becomes
    // the focused viewport unless a gesture holds the pointer. (Wheel carries
    // no position and keeps targeting the focused viewport downstream.)
    const auto resolve = [&](Vec2d position) -> ViewportId
    {
        if (router.PointerCaptured() && router.CaptureViewport().IsValid())
            return router.CaptureViewport();
        const ViewportId hovered = layout.ResolveAt(position);
        if (hovered.IsValid() && !router.PointerCaptured())
            layout.SetActive(hovered);
        return hovered;
    };

    if (auto* down = std::get_if<PointerDownEvent>(&event))
        down->Viewport = resolve(down->Position);
    else if (auto* up = std::get_if<PointerUpEvent>(&event))
        up->Viewport = resolve(up->Position);
    else if (auto* move = std::get_if<PointerMoveEvent>(&event))
        move->Viewport = resolve(move->Position);
    else if (auto* handed = std::get_if<PointerHandedEvent>(&event))
        handed->Viewport = resolve(handed->Position);
}
