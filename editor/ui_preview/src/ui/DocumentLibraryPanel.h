#pragma once

#include "authoring/DocumentLibrary.h"
#include "authoring/UiPreviewSession.h"

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>

//=============================================================================
// DocumentLibraryPanel
//
// The mounted libraries and their documents, one row each with its model name
// and whether a cook exists for it. Presentation only: opening, rescanning and
// launching the author's editor are the composition root's, by callback.
//=============================================================================
class DocumentLibraryPanel final : public IEditorPanel
{
public:
    struct Actions
    {
        std::function<void(const std::string& packagePath)> Open;
        std::function<void()> Rescan;
        std::function<void(const DocumentEntry&)> OpenInEditor;
    };

    DocumentLibraryPanel(DocumentLibrary& library, UiPreviewSession& session, Actions actions);

    [[nodiscard]] std::string_view GetTitle() const override { return "Documents"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "documents", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    void DrawRow(const DocumentEntry& entry);

    DocumentLibrary& Library;
    UiPreviewSession& Session;
    Actions Act;
    char FilterBuffer[128] = "";
};
