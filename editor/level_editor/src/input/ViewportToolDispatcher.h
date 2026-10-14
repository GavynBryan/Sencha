#pragma once

#include "input/GestureRecognizer.h"
#include "input/InputEvent.h"

#include <cstdint>

class EditSessionHost;
class InteractionHost;
class PointerCapture;
class ToolRegistry;
struct EditorViewport;
struct ITool;
struct ToolContext;
class ViewportLayout;

// Routes the views' pointer to the gizmo, the active tool and its interactions,
// owning each left-button gesture from press or hand-off to release
// (docs/editor/placement.md, Gesture lifecycle).
class ViewportToolDispatcher
{
public:
    ViewportToolDispatcher(ViewportLayout& layout,
                           ToolContext& context,
                           InteractionHost& interactions,
                           EditSessionHost& sessions,
                           ToolRegistry& tools);

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

private:
    enum class GestureSpan : std::uint8_t
    {
        None,
        PinnedToView,
        FollowsPointer,
    };

    InputConsumed HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerHanded(const PointerHandedEvent& e, PointerCapture& capture);
    InputConsumed HandleKeyDown(const KeyDownEvent& e, PointerCapture& capture);

    // The tool's hover preview (or the element glow) and gizmo highlight at `pos`.
    void HoverAt(EditorViewport& viewport, Vec2d pos);
    void ClearHover();

    // Resolves the element under the cursor for the active element mode and writes
    // it (plus an edge's length) to the overlay hover state, for the renderer glow.
    void UpdateHover(EditorViewport& viewport, Vec2d pos);

    // Shift+Right-click: sample the material the face under the cursor renders
    // with into the active material. View state only; not an undo step.
    void SampleFaceMaterial(const PointerDownEvent& e);

    // A gesture belongs to the tool that took it; another tool in hand ends it.
    [[nodiscard]] bool GestureOutlivedItsTool() const;
    // Ends the gesture in flight without committing anything: reverts any
    // interaction and drops the tool's gesture state. (W4.)
    void Abort();
    void AbortAndRelease(PointerCapture& capture);

    ViewportLayout& Layout;
    ToolContext& Context;
    InteractionHost& Interactions;
    EditSessionHost& Sessions;
    ToolRegistry& Tools;
    GestureRecognizer Recognizer;
    GestureSpan Span = GestureSpan::None;
    const ITool* GestureTool = nullptr;
};
