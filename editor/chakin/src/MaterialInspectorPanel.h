#pragma once

#include "MaterialTabSet.h"

#include "ui/IEditorPanel.h"

#include <assets/cook/TextureImportSettings.h>

#include <functional>

class AssetRegistry;

// Property editing over the active tab's working description. Widget edits
// apply to the session live (the preview follows every drag); when a widget
// deactivates after an edit, the activation-time snapshot and the final value
// become one EditMaterialCommand on that tab's undo stack. Texture rows also
// host the per-source import settings (usage/filter/compression/mips), applied
// through the composition root which recooks and hot-swaps the texture.
class MaterialInspectorPanel final : public IEditorPanel
{
public:
    using ApplyImportSettingsFn =
        std::function<void(const std::string& virtualPath, const TextureImportSettings&)>;

    MaterialInspectorPanel(MaterialTabSet& tabs, const AssetRegistry& registry,
                           ApplyImportSettingsFn applyImportSettings);

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

    void DrawImportSettingsPopup();
    void RequestImportSettings(const std::string& virtualPath);

    MaterialTabSet& Tabs;
    const AssetRegistry& Registry;
    ApplyImportSettingsFn ApplyImportSettings;

    // Working-state snapshot at widget activation: the undo command's "before".
    MaterialDescription EditBaseline;
    bool BaselineCaptured = false;
    char TextureFilterText[128] = "";

    // Import-settings popup state (opened from a picker row's context menu).
    std::string ImportTarget; // virtual path; empty = closed
    bool ImportPopupPending = false;
    TextureImportSettings ImportDraft;
};
