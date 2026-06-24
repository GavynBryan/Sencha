#pragma once

#include "../../tools/ITool.h"
#include "../../brush/BrushMesh.h"

#include <ecs/EntityId.h>

// Click-to-cut edge tool. Its own mode: hovering a brush edge shows a live preview
// of the cut at the cursor's position along the edge (PreviewMesh on the live
// scene), a click commits it (CommitMesh, undoable), and a hotkey/toolbar toggle
// picks loop vs single. Works on any edge under the cursor.
class EdgeCutTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    std::string_view GetIcon() const override;

    InputConsumed OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnHoverEnd(ToolContext& ctx) override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) override;
    void OnDeactivate(ToolContext& ctx) override;
    void OnCancel(ToolContext& ctx) override;

private:
    // Revert any active preview, pick the edge under the cursor, and preview its cut.
    void UpdatePreview(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    void Commit(ToolContext& ctx);
    void Revert(ToolContext& ctx);

    EntityId PreviewEntity = {}; // the brush currently showing a preview, if any
    BrushMesh Original;          // its pre-cut mesh (to revert and as the commit's "before")
    BrushMesh Pending;           // the cut to commit
    bool PendingValid = false;
};
