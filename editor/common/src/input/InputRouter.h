#pragma once

#include "InputEvent.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

// What kind of owner currently holds the pointer. A UI drag (Ui) keeps the UI
// live; a viewport gesture (Viewport) — fly-look, ortho-pan, or a tool drag — is
// exclusive of the UI, so the app gates ImGui's mouse off while one is held.
enum class PointerCaptureKind
{
    Ui,
    Viewport,
};

class InputRouter;

// Handle given to a handler for the event it is processing. The handler that
// begins a gesture calls Acquire() to take exclusive ownership of the pointer; the
// router then delivers every pointer event to that handler alone until it
// Release()s (or focus is lost). This is the single home for "who owns the
// pointer" — the former ad-hoc capture mechanisms (UI drag stickiness, fly/pan
// flags, tool gesture pinning) all fold into it.
class PointerCapture
{
public:
    // `origin` is the viewport the gesture began in; the router stamps it onto
    // subsequent pointer events so the gesture stays on its starting viewport.
    void Acquire(PointerCaptureKind kind, ViewportId origin = {});
    void Release();
    [[nodiscard]] bool HeldBySelf() const;

private:
    friend class InputRouter;
    PointerCapture(InputRouter& router, std::size_t handlerIndex)
        : Router(router)
        , Index(handlerIndex)
    {
    }

    InputRouter& Router;
    std::size_t Index;
};

// Ordered handler chain with first-class pointer capture. With no capture held,
// pointer events walk the chain by priority until one consumes; once a handler
// captures, all pointer events route to it exclusively. Keyboard and focus-lost
// events always walk the chain (focus loss additionally drops any capture).
class InputRouter
{
public:
    using Handler = std::function<InputConsumed(const InputEvent&, PointerCapture&)>;

    void AddHandler(Handler handler);
    InputConsumed Route(const InputEvent& event);

    // Whether a pointer capture is currently held, and the viewport its holder
    // recorded at Acquire. Used by the input boundary to stamp events while captured.
    [[nodiscard]] bool PointerCaptured() const { return CapturedIndex.has_value(); }
    [[nodiscard]] ViewportId CaptureViewport() const { return CaptureOrigin; }

    // Notified when the capture owner changes: the new kind, or nullopt on release.
    // Drives the ImGui mouse gate (off while a Viewport capture is held).
    void SetCaptureChanged(std::function<void(std::optional<PointerCaptureKind>)> callback);

private:
    friend class PointerCapture;
    void AcquireFor(std::size_t index, PointerCaptureKind kind, ViewportId origin);
    void ReleaseFor(std::size_t index);
    [[nodiscard]] static bool IsPointerEvent(const InputEvent& event);

    std::vector<Handler> Handlers;
    std::optional<std::size_t> CapturedIndex;
    PointerCaptureKind CapturedKind = PointerCaptureKind::Viewport;
    ViewportId CaptureOrigin = {};
    std::function<void(std::optional<PointerCaptureKind>)> CaptureChanged;
};
