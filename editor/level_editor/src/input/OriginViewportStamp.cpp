#include "input/OriginViewportStamp.h"

#include "input/InputRouter.h"
#include "viewport/ViewportLayout.h"

#include <variant>

void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event)
{
    // While a gesture holds the pointer, every event belongs to the viewport it began
    // in; otherwise a positioned event belongs to the viewport under the cursor, which
    // also becomes the focused viewport. (Wheel carries no position and keeps
    // targeting the focused viewport downstream.)
    const auto resolve = [&](ImVec2 position) -> ViewportId
    {
        if (router.PointerCaptured())
            return router.CaptureViewport();
        const ViewportId hovered = layout.ResolveAt(position);
        if (hovered.IsValid())
            layout.SetActive(hovered);
        return hovered;
    };

    if (auto* down = std::get_if<PointerDownEvent>(&event))
        down->Viewport = resolve(down->Position);
    else if (auto* up = std::get_if<PointerUpEvent>(&event))
        up->Viewport = resolve(up->Position);
    else if (auto* move = std::get_if<PointerMoveEvent>(&event))
        move->Viewport = resolve(move->Position);
}
