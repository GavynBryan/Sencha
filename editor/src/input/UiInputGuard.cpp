#include "UiInputGuard.h"

#include <type_traits>
#include <utility>
#include <variant>

// Capture (owned by the InputRouter) now provides the drag stickiness this guard
// used to track by hand: when the UI owns a press, it Acquires, so the router
// keeps routing the gesture to the UI even as the cursor crosses out of the panel.
InputRouter::Handler MakeUiInputGuard(std::function<UiInputCapture()> capture)
{
    return [capture = std::move(capture)](const InputEvent& event,
                                          PointerCapture& pointerCapture) -> InputConsumed
    {
        const UiInputCapture ui = capture();

        const bool consumed = std::visit(
            [&](const auto& e) -> bool
            {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, PointerDownEvent>)
                {
                    if (!ui.Mouse)
                        return false;
                    // The UI owns this gesture; hold it for the drag's duration.
                    pointerCapture.Acquire(PointerCaptureKind::Ui);
                    return true;
                }
                else if constexpr (std::is_same_v<E, PointerMoveEvent>)
                {
                    // Mid-drag the press owner keeps the pointer; otherwise hover decides.
                    return pointerCapture.HeldBySelf() || ui.Mouse;
                }
                else if constexpr (std::is_same_v<E, PointerUpEvent>)
                {
                    if (pointerCapture.HeldBySelf())
                    {
                        pointerCapture.Release();
                        return true;
                    }
                    return ui.Mouse;
                }
                else if constexpr (std::is_same_v<E, WheelEvent>)
                {
                    return ui.Mouse;
                }
                else if constexpr (std::is_same_v<E, KeyDownEvent>)
                {
                    return ui.Keyboard;
                }
                else // FocusLostEvent — abandon any in-progress UI drag.
                {
                    if (pointerCapture.HeldBySelf())
                        pointerCapture.Release();
                    return false;
                }
            },
            event);

        return consumed ? InputConsumed::Yes : InputConsumed::No;
    };
}
