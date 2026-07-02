#pragma once

#include "MaterialTabSet.h"

#include "ui/IEditorPanel.h"

class AssetRegistry;

// Property editing over the active tab's working description. Widget edits
// apply to the session live (the preview follows every drag); when a widget
// deactivates after an edit, the activation-time snapshot and the final value
// become one EditMaterialCommand on that tab's undo stack.
class MaterialInspectorPanel final : public IEditorPanel
{
public:
    MaterialInspectorPanel(MaterialTabSet& tabs, const AssetRegistry& registry);

    [[nodiscard]] std::string_view GetTitle() const override { return "Inspector"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override;

private:
    // Tracks activate/deactivate around one widget: call after each widget with
    // the (possibly edited) copy; pushes the undo command on deactivation.
    void CommitWidgetEdit(MaterialEditTab& tab, MaterialDescription& edited);
    // id must be unique across the whole panel (CollapsingHeader does not push
    // an ID scope, so a repeated label alone collides).
    void DrawTextureSlot(MaterialEditTab& tab, const char* id, const char* label,
                         AssetRef& slot, MaterialDescription& edited);

    MaterialTabSet& Tabs;
    const AssetRegistry& Registry;

    // Working-state snapshot at widget activation: the undo command's "before".
    MaterialDescription EditBaseline;
    bool BaselineCaptured = false;
    char TextureFilter[128] = "";
};
