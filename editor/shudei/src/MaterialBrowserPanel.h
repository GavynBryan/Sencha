#pragma once

#include "MaterialTabSet.h"

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>

class MaterialLibrary;

// Project material list plus the create/duplicate/rename entry points. The
// panel stays presentation-only: every action is a callback the composition
// root wires (it owns registries, the tabs, and the preview).
class MaterialBrowserPanel final : public IEditorPanel
{
public:
    struct Actions
    {
        std::function<void(const std::string& virtualPath)> Open;
        std::function<void(const std::string& name)> CreateNew;
        std::function<void(const std::string& name)> Duplicate;
        // Move/rename to a new content-root-relative path (folders included).
        std::function<void(const std::string& virtualPath, const std::string& newRelPath)> Rename;
        std::function<void()> Rescan;
    };

    MaterialBrowserPanel(MaterialLibrary& materials,
                         MaterialTabSet& tabs,
                         Actions actions);

    [[nodiscard]] std::string_view GetTitle() const override { return "Materials"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override;

private:
    void DrawMaterialList();
    void DrawMaterialRow(const char* label, const std::string& virtualPath,
                         const std::string& displayName);
    void DrawRenamePopup();

    MaterialLibrary& Materials;
    MaterialTabSet& Tabs;
    Actions Act;
    char NameBuffer[128] = "new_material";
    char FilterBuffer[128] = "";

    // Rename popup state (opened from a row's context menu).
    std::string RenameTarget; // virtual path being renamed; empty = closed
    bool RenamePopupPending = false;
    char RenameBuffer[256] = "";
};
