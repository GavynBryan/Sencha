#pragma once

#include "../../tools/ITool.h"

// The selection tool. A click is a single pick under the cursor; dragging past the
// gesture deadzone runs a marquee box-select (a MarqueeInteraction). Both resolve
// per the active element mode, with Shift = add and Ctrl = remove. A manipulator
// drag, when hit, runs before the tool (the session consumes the press first), so
// this only fires on empty/selection input.
class SelectTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    std::string_view GetIcon() const override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    // Double-click: edge -> its loop, face -> all faces (or the face loop if it is
    // already selected), vertex -> all vertices.
    InputConsumed OnDoubleClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer) override;
};
