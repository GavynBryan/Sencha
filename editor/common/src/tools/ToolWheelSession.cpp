#include "ToolWheelSession.h"

#include "ToolRegistry.h"

#include "input/InputRouter.h"

#include <algorithm>
#include <utility>

ToolWheelSession::ToolWheelSession(ToolRegistry& tools, ITool::Shortcut key,
                                   std::function<ToolWheel::Frame()> frame,
                                   std::function<bool(ImVec2)> pointerOnScene)
    : Tools(tools)
    , Key(key)
    , FrameProvider(std::move(frame))
    , PointerOnScene(std::move(pointerOnScene))
{
}

InputConsumed ToolWheelSession::OnInput(const InputEvent& event, PointerCapture& capture)
{
    if (const auto* key = std::get_if<KeyDownEvent>(&event))
        return OnKeyDown(*key, capture);
    if (const auto* key = std::get_if<KeyUpEvent>(&event))
        return OnKeyUp(*key, capture);
    if (std::get_if<FocusLostEvent>(&event))
    {
        // Focus loss also loses the key's release. Not consumed: the router
        // walks every handler on it.
        if (Phase != ToolWheelPhase::Closed)
            Leave(ToolWheelPhase::Closed, capture);
        return InputConsumed::No;
    }

    if (Phase != ToolWheelPhase::Open)
        return InputConsumed::No;

    // Pointer events reach here exclusively while the wheel is open. Motion
    // picks; everything else is swallowed so nothing under the wheel reacts.
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
        Track(move->Position);
    return InputConsumed::Yes;
}

InputConsumed ToolWheelSession::OnKeyDown(const KeyDownEvent& event, PointerCapture& capture)
{
    switch (Phase)
    {
    case ToolWheelPhase::Closed:
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

    case ToolWheelPhase::Open:
        if (event.Key == SDLK_ESCAPE)
            Leave(ToolWheelPhase::Dismissed, capture);
        // Modal: no other key acts while the wheel is up.
        return InputConsumed::Yes;

    case ToolWheelPhase::Dismissed:
        // The key has not been released, so a press of it (a repeat, or one the
        // boundary missed the release of) is not a fresh gesture.
        return IsWheelKey(event.Key) ? InputConsumed::Yes : InputConsumed::No;
    }
    return InputConsumed::No;
}

InputConsumed ToolWheelSession::OnKeyUp(const KeyUpEvent& event, PointerCapture& capture)
{
    // Modifiers are ignored on release: the gesture ends when the key does,
    // whatever else the hand is holding by then.
    if (Phase == ToolWheelPhase::Closed || !IsWheelKey(event.Key))
        return InputConsumed::No;
    if (Phase == ToolWheelPhase::Open && Hot >= 0)
        Select(Hot, HotVariant);
    Leave(ToolWheelPhase::Closed, capture);
    return InputConsumed::Yes;
}

void ToolWheelSession::Open(ImVec2 pointer, PointerCapture& capture)
{
    const int count = static_cast<int>(Tools.GetTools().size());
    int maxVariants = 0;
    for (int i = 0; i < count; ++i)
        maxVariants = std::max(maxVariants, VariantCount(i));
    Layout = ToolWheel::Place(FrameProvider ? FrameProvider() : ToolWheel::Frame{}, pointer, count, maxVariants);
    Hot = -1;
    HotVariant = -1;
    Phase = ToolWheelPhase::Open;
    capture.Acquire(PointerCaptureKind::Exclusive);
}

void ToolWheelSession::Leave(ToolWheelPhase next, PointerCapture& capture)
{
    if (capture.HeldBySelf())
        capture.Release();
    Hot = -1;
    HotVariant = -1;
    Phase = next;
}

void ToolWheelSession::Track(ImVec2 pointer)
{
    switch (ToolWheel::RingAt(Layout, pointer))
    {
    case ToolWheel::Ring::Hub:
        Hot = -1;
        HotVariant = -1;
        return;
    case ToolWheel::Ring::Primary:
        Hot = ToolWheel::SectorAt(Layout, pointer);
        HotVariant = -1;
        return;
    case ToolWheel::Ring::Outer:
        break;
    }
    // The hot tool keeps the pointer while it is over that tool's own fan,
    // which is what lets the pointer leave the parent's wedge for a child on
    // its flank; past the fan's edge, direction picks the tool as it does
    // inside, and a tool picked that way has a variant only where its own
    // fan lies in that direction.
    const bool held = Hot >= 0 && ToolWheel::VariantAt(Layout, Hot, VariantCount(Hot), pointer) >= 0;
    if (!held)
        Hot = ToolWheel::SectorAt(Layout, pointer);
    HotVariant = Hot >= 0 ? ToolWheel::VariantAt(Layout, Hot, VariantCount(Hot), pointer) : -1;
}

void ToolWheelSession::Select(int index, int variant)
{
    // A variant is chosen for the work that follows: the registry enters the
    // tool or places what it has staged, then selects the variant. The tool
    // alone is entered only if it is not already on.
    if (variant >= 0)
        (void)Tools.SelectVariant(static_cast<std::size_t>(index), static_cast<std::size_t>(variant));
    else if (index != Tools.GetActiveIndex())
        (void)Tools.Activate(static_cast<std::size_t>(index));
}

int ToolWheelSession::VariantCount(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= Tools.GetTools().size())
        return 0;
    const ITool* tool = Tools.GetTools()[static_cast<std::size_t>(index)].get();
    return tool != nullptr ? static_cast<int>(tool->GetVariants().size()) : 0;
}
