#pragma once

#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

//=============================================================================
// ActionLogPanel
//
// Every action the open document raised, with its arguments and their kinds:
// the proof that a control is wired to what the host would receive.
//=============================================================================
class ActionLogPanel final : public IEditorPanel
{
public:
    explicit ActionLogPanel(UiPreviewSession& session);

    [[nodiscard]] std::string_view GetTitle() const override { return "Actions"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "actions", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    UiPreviewSession& Session;
};
