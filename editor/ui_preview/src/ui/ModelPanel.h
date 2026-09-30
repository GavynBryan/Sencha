#pragma once

#include "PreviewViewState.h"
#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

#include <cstddef>
#include <functional>
#include <string>

//=============================================================================
// ModelPanel
//
// The preview model the open document is shown against, edited in place.
// Lists and rows publish as they change; a property's declaration is part of
// the screen, so a property edit remakes the screen against the new model.
// The Bindings tab is the runtime's list of what the document asked for and
// the model did not declare -- observed so far, never statically complete.
//=============================================================================
class ModelPanel final : public IEditorPanel
{
public:
    struct Actions
    {
        std::function<bool(std::string* error)> Save;
        std::function<void()> Reset;
    };

    ModelPanel(UiPreviewSession& session, PreviewViewState& view, Actions actions);

    [[nodiscard]] std::string_view GetTitle() const override { return "Model"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::RightBottom; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "model", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    void DrawModelTab();
    void DrawBindingsTab();
    void DrawProperties();
    void DrawArrays();
    void DrawRows();
    void DrawActions();
    // A change to the declaration: the screen is remade against it.
    void Redeclare();
    // A change to a sample list or rows: published to the open screen.
    void Republish();

    UiPreviewSession& Session;
    PreviewViewState& View;
    Actions Act;
    std::string LastError;
    char NewNameBuffer[96] = "";
};
