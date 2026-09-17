#pragma once

#include "PreviewViewState.h"
#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

//=============================================================================
// ElementPanel
//
// The selected element: its boxes in surface pixels and in dp at the
// session's display scale, the computed values a layout question needs, and
// the two authoring traps a document is most often caught by -- an activatable
// element a controller cannot reach, and a root that swallows every pointer.
//=============================================================================
class ElementPanel final : public IEditorPanel
{
public:
    ElementPanel(UiPreviewSession& session, PreviewViewState& view);

    [[nodiscard]] std::string_view GetTitle() const override { return "Element"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Right; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "element", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    void DrawBoxes(const UiElementInfo& info);
    void DrawComputed(const UiElementInfo& info);
    void DrawNotes(const UiElementInfo& info);

    UiPreviewSession& Session;
    PreviewViewState& View;
};
