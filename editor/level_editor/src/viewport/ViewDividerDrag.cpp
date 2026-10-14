#include "viewport/ViewDividerDrag.h"

#include "input/InputRouter.h"

#include <variant>

namespace
{
// How far either side of a divider a press still takes it, in window pixels.
constexpr float kReach = 5.0f;
} // namespace

ViewDividerDrag::ViewDividerDrag(std::function<Arrangement()> arrangement, std::function<void(Vec2d)> moved)
    : Current(std::move(arrangement))
    , Moved(std::move(moved))
{
}

InputConsumed ViewDividerDrag::OnInput(const InputEvent& event, PointerCapture& capture)
{
    // The router takes the pointer back on focus loss; the divider stays where
    // it was dragged to.
    if (std::holds_alternative<FocusLostEvent>(event))
    {
        Held = {};
        return InputConsumed::No;
    }
    if (const auto* down = std::get_if<PointerDownEvent>(&event))
    {
        if (down->Button != MouseButton::Left || capture.HeldByOther())
            return InputConsumed::No;
        const Arrangement now = Current();
        const DividerGrip grip = GripDivider(now.Kind, now.Content, now.Divider, down->Position, kReach);
        if (!grip.Any())
            return InputConsumed::No;
        Held = grip;
        capture.Acquire(PointerCaptureKind::Exclusive);
        return InputConsumed::Yes;
    }
    if (!Held.Any() || !capture.HeldBySelf())
        return InputConsumed::No;
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        const Arrangement now = Current();
        Moved(MoveDivider(now.Content, now.Divider, Held, move->Position));
        return InputConsumed::Yes;
    }
    if (const auto* up = std::get_if<PointerUpEvent>(&event); up != nullptr && up->Button == MouseButton::Left)
    {
        Held = {};
        capture.Release();
        return InputConsumed::Yes;
    }
    return InputConsumed::No;
}
