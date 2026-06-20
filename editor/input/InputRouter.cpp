#include "InputRouter.h"

#include <variant>

void PointerCapture::Acquire(PointerCaptureKind kind, ViewportId origin)
{
    Router.AcquireFor(Index, kind, origin);
}

void PointerCapture::Release()
{
    Router.ReleaseFor(Index);
}

bool PointerCapture::HeldBySelf() const
{
    return Router.CapturedIndex.has_value() && *Router.CapturedIndex == Index;
}

void InputRouter::AddHandler(Handler handler)
{
    Handlers.push_back(std::move(handler));
}

bool InputRouter::IsPointerEvent(const InputEvent& event)
{
    return std::holds_alternative<PointerDownEvent>(event)
        || std::holds_alternative<PointerUpEvent>(event)
        || std::holds_alternative<PointerMoveEvent>(event)
        || std::holds_alternative<WheelEvent>(event);
}

void InputRouter::AcquireFor(std::size_t index, PointerCaptureKind kind, ViewportId origin)
{
    const bool changed =
        !CapturedIndex.has_value() || *CapturedIndex != index || CapturedKind != kind;
    CapturedIndex = index;
    CapturedKind = kind;
    CaptureOrigin = origin;
    if (changed && CaptureChanged)
        CaptureChanged(kind);
}

void InputRouter::ReleaseFor(std::size_t index)
{
    if (!CapturedIndex.has_value() || *CapturedIndex != index)
        return;
    CapturedIndex.reset();
    if (CaptureChanged)
        CaptureChanged(std::nullopt);
}

InputConsumed InputRouter::Route(const InputEvent& event)
{
    // A held capture takes every pointer event exclusively — the rest of the chain
    // does not run, so "the camera/tool owns the pointer" needs no downstream guard.
    if (IsPointerEvent(event) && CapturedIndex.has_value())
    {
        const std::size_t index = *CapturedIndex;
        PointerCapture capture(*this, index);
        return Handlers[index](event, capture);
    }

    // Focus loss tears down any in-flight gesture: let every handler reset, then
    // drop capture so the next frame starts clean.
    if (std::holds_alternative<FocusLostEvent>(event))
    {
        for (std::size_t i = 0; i < Handlers.size(); ++i)
        {
            PointerCapture capture(*this, i);
            Handlers[i](event, capture);
        }
        if (CapturedIndex.has_value())
            ReleaseFor(*CapturedIndex);
        return InputConsumed::No;
    }

    for (std::size_t i = 0; i < Handlers.size(); ++i)
    {
        PointerCapture capture(*this, i);
        if (Handlers[i](event, capture) == InputConsumed::Yes)
            return InputConsumed::Yes;
    }
    return InputConsumed::No;
}

void InputRouter::SetCaptureChanged(std::function<void(std::optional<PointerCaptureKind>)> callback)
{
    CaptureChanged = std::move(callback);
}
