#include "ToolWheelSession.h"

#include "ToolRegistry.h"

#include "input/InputRouter.h"

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
        Hot = ToolWheel::SectorAt(Layout, move->Position);
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
        Select(Hot);
    Leave(ToolWheelPhase::Closed, capture);
    return InputConsumed::Yes;
}

void ToolWheelSession::Open(ImVec2 pointer, PointerCapture& capture)
{
    Layout = ToolWheel::Place(FrameProvider ? FrameProvider() : ToolWheel::Frame{}, pointer,
                              static_cast<int>(Tools.GetTools().size()));
    Hot = -1;
    Phase = ToolWheelPhase::Open;
    capture.Acquire(PointerCaptureKind::Exclusive);
}

void ToolWheelSession::Leave(ToolWheelPhase next, PointerCapture& capture)
{
    if (capture.HeldBySelf())
        capture.Release();
    Hot = -1;
    Phase = next;
}

void ToolWheelSession::Select(int index)
{
    if (index == Tools.GetActiveIndex())
        return;
    (void)Tools.Activate(static_cast<std::size_t>(index));
}
