#include "UiInputGuard.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    // Pointer-capture state: which side owns the in-progress mouse gesture. The
    // owner is fixed on the first button press and held until all buttons release,
    // so a drag started in the viewport (a tool, or camera) keeps the pointer even
    // as the cursor crosses a UI panel — and vice versa. This is the same "mouse
    // capture" rule OS UI frameworks use.
    struct GuardState
    {
        int  ButtonsDown   = 0;
        bool DragOwnedByUi = false;
    };
}

InputRouter::Handler MakeUiInputGuard(std::function<UiInputCapture()> capture)
{
    auto state = std::make_shared<GuardState>();

    return [capture = std::move(capture), state](const InputEvent& event) -> InputConsumed
    {
        const UiInputCapture ui = capture();

        const bool uiOwns = std::visit(
            [&](const auto& e) -> bool
            {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, PointerDownEvent>)
                {
                    if (state->ButtonsDown == 0)
                        state->DragOwnedByUi = ui.Mouse; // owner fixed at gesture start
                    ++state->ButtonsDown;
                    return state->DragOwnedByUi;
                }
                else if constexpr (std::is_same_v<E, PointerUpEvent>)
                {
                    const bool dragging = state->ButtonsDown > 0;
                    const bool owned = dragging ? state->DragOwnedByUi : ui.Mouse;
                    if (dragging)
                        --state->ButtonsDown;
                    return owned;
                }
                else if constexpr (std::is_same_v<E, PointerMoveEvent>)
                {
                    // Mid-gesture the press owner keeps the pointer; otherwise hover decides.
                    return state->ButtonsDown > 0 ? state->DragOwnedByUi : ui.Mouse;
                }
                else if constexpr (std::is_same_v<E, WheelEvent>)
                {
                    return ui.Mouse;
                }
                else if constexpr (std::is_same_v<E, KeyDownEvent>)
                {
                    return ui.Keyboard;
                }
                else // FocusLostEvent — abandon any in-progress capture.
                {
                    state->ButtonsDown = 0;
                    return false;
                }
            },
            event);

        return uiOwns ? InputConsumed::Yes : InputConsumed::No;
    };
}
