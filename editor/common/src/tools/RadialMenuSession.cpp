#include "RadialMenuSession.h"

#include "input/InputRouter.h"

#include <algorithm>
#include <utility>

RadialMenuSession::RadialMenuSession(IRadialMenuModel& menu, KeyChord key,
                                     std::function<RadialMenu::Frame()> frame,
                                     std::function<bool(ImVec2)> pointerOnScene)
    : Menu(menu)
    , Key(key)
    , FrameProvider(std::move(frame))
    , PointerOnScene(std::move(pointerOnScene))
{
}

InputConsumed RadialMenuSession::OnInput(const InputEvent& event, PointerCapture& capture)
{
    if (const auto* key = std::get_if<KeyDownEvent>(&event))
        return OnKeyDown(*key, capture);
    if (const auto* key = std::get_if<KeyUpEvent>(&event))
        return OnKeyUp(*key, capture);
    if (std::get_if<FocusLostEvent>(&event))
    {
        // Focus loss also loses the key's release. Not consumed: the router
        // walks every handler on it.
        if (Phase != RadialMenuPhase::Closed)
            Leave(RadialMenuPhase::Closed, capture);
        return InputConsumed::No;
    }

    if (Phase != RadialMenuPhase::Open)
        return InputConsumed::No;

    // Pointer events reach here exclusively while the wheel is open. Motion
    // picks; everything else is swallowed so nothing under the wheel reacts.
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
        Track(move->Position);
    return InputConsumed::Yes;
}

InputConsumed RadialMenuSession::OnKeyDown(const KeyDownEvent& event, PointerCapture& capture)
{
    switch (Phase)
    {
    case RadialMenuPhase::Closed:
        if (!IsWheelKey(event.Key) || !(event.Modifiers == Key.Mods))
            return InputConsumed::No;
        // A gesture in flight keeps its key: Q descends the fly camera, and
        // the wheel waits for a press with the pointer free.
        if (capture.HeldByOther())
            return InputConsumed::No;
        // Off the scene the key is the wheel's and does nothing: no open, no
        // capture, and nothing behind gets to act on it either.
        if (!PointerOnScene || !PointerOnScene(event.Pointer))
            return InputConsumed::Yes;
        Open(event.Pointer, capture);
        return InputConsumed::Yes;

    case RadialMenuPhase::Open:
        if (event.Key == SDLK_ESCAPE)
            Leave(RadialMenuPhase::Dismissed, capture);
        // Modal: no other key acts while the wheel is up.
        return InputConsumed::Yes;

    case RadialMenuPhase::Dismissed:
        // The key has not been released, so a press of it (a repeat, or one the
        // boundary missed the release of) is not a fresh gesture.
        return IsWheelKey(event.Key) ? InputConsumed::Yes : InputConsumed::No;
    }
    return InputConsumed::No;
}

InputConsumed RadialMenuSession::OnKeyUp(const KeyUpEvent& event, PointerCapture& capture)
{
    // Modifiers are ignored on release: the gesture ends when the key does,
    // whatever else the hand is holding by then.
    if (Phase == RadialMenuPhase::Closed || !IsWheelKey(event.Key))
        return InputConsumed::No;
    if (Phase == RadialMenuPhase::Open && Hot >= 0)
        Select(Hot, HotVariant);
    Leave(RadialMenuPhase::Closed, capture);
    return InputConsumed::Yes;
}

void RadialMenuSession::Open(ImVec2 pointer, PointerCapture& capture)
{
    const int count = Menu.Count();
    int maxVariants = 0;
    for (int i = 0; i < count; ++i)
        maxVariants = std::max(maxVariants, VariantCount(i));
    Layout = RadialMenu::Place(FrameProvider ? FrameProvider() : RadialMenu::Frame{}, pointer, count, maxVariants);
    Hot = -1;
    HotVariant = -1;
    Phase = RadialMenuPhase::Open;
    capture.Acquire(PointerCaptureKind::Exclusive);
}

void RadialMenuSession::Leave(RadialMenuPhase next, PointerCapture& capture)
{
    if (capture.HeldBySelf())
        capture.Release();
    Hot = -1;
    HotVariant = -1;
    Phase = next;
}

void RadialMenuSession::Track(ImVec2 pointer)
{
    switch (RadialMenu::RingAt(Layout, pointer))
    {
    case RadialMenu::Ring::Hub:
        Hot = -1;
        HotVariant = -1;
        return;
    case RadialMenu::Ring::Primary:
        Hot = RadialMenu::SectorAt(Layout, pointer);
        HotVariant = -1;
        return;
    case RadialMenu::Ring::Outer:
        break;
    }
    // The hot entry keeps the pointer while it is over its own fan, which is
    // what lets the pointer leave the parent's wedge for a child on its
    // flank; past the fan's edge, direction picks the entry as it does
    // inside, and an entry picked that way has a variant only where its own
    // fan lies in that direction.
    const bool held = Hot >= 0 && RadialMenu::VariantAt(Layout, Hot, VariantCount(Hot), pointer) >= 0;
    if (!held)
        Hot = RadialMenu::SectorAt(Layout, pointer);
    HotVariant = Hot >= 0 ? RadialMenu::VariantAt(Layout, Hot, VariantCount(Hot), pointer) : -1;
}

void RadialMenuSession::Select(int index, int variant)
{
    Menu.Select(index, variant);
}

int RadialMenuSession::VariantCount(int index) const
{
    if (index < 0 || index >= Menu.Count())
        return 0;
    return static_cast<int>(Menu.Variants(index).size());
}
