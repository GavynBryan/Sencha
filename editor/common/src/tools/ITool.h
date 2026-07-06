#pragma once

#include "input/InputEvent.h"
#include "interaction/IInteraction.h"

#include <imgui.h>

#include <memory>
#include <string_view>

struct EditorViewport;
struct ToolContext;

struct ITool
{
    virtual std::string_view GetId() const = 0;
    virtual std::string_view GetDisplayName() const = 0;

    // Font Awesome glyph (an ICON_FA_* literal from fonts/IconsFontAwesome6.h) for
    // the toolbar. Empty -> the toolbar falls back to the display name text.
    virtual std::string_view GetIcon() const { return {}; }

    virtual void OnActivate(ToolContext& ctx) {}
    virtual void OnDeactivate(ToolContext& ctx) {}

    // Abort any in-progress gesture (e.g. a rubber-band drag) and drop its
    // transient state. Called on Escape and focus loss so a tool gesture can never
    // be left dangling. (W4.)
    virtual void OnCancel(ToolContext& ctx) {}

    // Semantic pointer gestures from the GestureRecognizer (the click-vs-drag
    // deadzone + double-click timing live there, not per tool). A click is a press
    // and release under the deadzone. BeginDrag fires when a press crosses it and
    // returns the interaction that runs the drag — null if this tool doesn't drag.
    virtual InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) { return InputConsumed::No; }
    virtual InputConsumed OnDoubleClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) { return InputConsumed::No; }
    virtual std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer) { return nullptr; }
    virtual InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) { return InputConsumed::No; }

    // Cursor motion with no button held. A tool that drives a live hover preview
    // (e.g. the edge cut) handles it and returns Yes, which suppresses the default
    // element-hover glow. OnHoverEnd fires when the cursor leaves the viewport, a
    // drag starts, or the tool deactivates, so the tool can drop its preview.
    virtual InputConsumed OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) { return InputConsumed::No; }
    virtual void OnHoverEnd(ToolContext& ctx) {}

    virtual ~ITool() = default;
};
