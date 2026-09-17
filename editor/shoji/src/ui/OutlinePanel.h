#pragma once

#include "PreviewViewState.h"
#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

#include <vector>

//=============================================================================
// OutlinePanel
//
// The open document's element tree as the layer reports it this frame. Hover
// and selection are the shared view state, so the Preview outlines what this
// panel points at and this panel highlights what the Preview picked.
//=============================================================================
class OutlinePanel final : public IEditorPanel
{
public:
    OutlinePanel(UiPreviewSession& session, PreviewViewState& view);

    [[nodiscard]] std::string_view GetTitle() const override { return "Outline"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Right; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "outline", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    void DrawNode(const std::vector<UiElementInfo>& tree, std::size_t index);

    UiPreviewSession& Session;
    PreviewViewState& View;
    bool HoveredThisFrame = false;
};
