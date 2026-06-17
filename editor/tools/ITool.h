#pragma once

#include "../input/InputEvent.h"

#include <imgui.h>

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
    // transient state. Called on Escape and focus loss so a tool drag, like an
    // interaction, can never be left dangling. (W4.)
    virtual void OnCancel(ToolContext& ctx) {}

    virtual InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) { return InputConsumed::No; }
    virtual InputConsumed OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) { return InputConsumed::No; }
    virtual InputConsumed OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) { return InputConsumed::No; }
    virtual InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) { return InputConsumed::No; }

    virtual ~ITool() = default;
};
