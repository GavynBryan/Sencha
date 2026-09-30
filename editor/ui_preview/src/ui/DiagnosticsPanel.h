#pragma once

#include "PreviewViewState.h"
#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>

//=============================================================================
// DiagnosticsPanel
//
// The layer's reports about the open document, from the session's history:
// kind, message, and whichever of path:line, screen and surface the engine
// actually knew. A row offers only what its attribution supports.
//=============================================================================
class DiagnosticsPanel final : public IEditorPanel
{
public:
    DiagnosticsPanel(UiPreviewSession& session,
                     PreviewViewState& view,
                     std::function<void(const std::string& path)> openPath);

    [[nodiscard]] std::string_view GetTitle() const override { return "Diagnostics"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "diagnostics", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    UiPreviewSession& Session;
    PreviewViewState& View;
    std::function<void(const std::string& path)> OpenPath;
    bool ShowInfo = false;
};
