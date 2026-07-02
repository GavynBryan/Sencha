#pragma once

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>

class MaterialLibrary;
class MaterialEditSession;

// Project material list plus the create/duplicate entry points. The panel
// stays presentation-only: every action is a callback the composition root
// wires (it owns registries, the session, and the preview).
class MaterialBrowserPanel final : public IEditorPanel
{
public:
    struct Actions
    {
        std::function<void(const std::string& virtualPath)> Open;
        std::function<void(const std::string& name)> CreateNew;
        std::function<void(const std::string& name)> Duplicate;
        std::function<void()> Rescan;
    };

    MaterialBrowserPanel(MaterialLibrary& materials,
                         const MaterialEditSession& session,
                         Actions actions);

    [[nodiscard]] std::string_view GetTitle() const override { return "Materials"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override;

private:
    void DrawMaterialList();

    MaterialLibrary& Materials;
    const MaterialEditSession& Session;
    Actions Act;
    char NameBuffer[128] = "new_material";
    char FilterBuffer[128] = "";
};
