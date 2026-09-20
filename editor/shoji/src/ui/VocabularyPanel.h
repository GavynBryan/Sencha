#pragma once

#include "authoring/VocabularyCatalog.h"

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>
#include <vector>

//=============================================================================
// VocabularyPanel
//
// Read-only: the verbs the loaded vocabulary offers, and every binding asset
// the previewer can see with each record marked resolved or not, against the
// same catalog a project's documents would author against. Nothing here
// invokes anything, and nothing here edits: an authored record belongs to a
// document, and changing one is a command for the editor that owns it.
//=============================================================================
class VocabularyPanel final : public IEditorPanel
{
public:
    struct BindingAsset
    {
        std::string Path;
        std::vector<VocabularyCatalog::BindingRow> Records;
    };

    // `bindingAssets` is asked when the panel refreshes: the host enumerates
    // the assets it mounted and inspects each through the catalog.
    VocabularyPanel(const VocabularyCatalog& catalog,
                    std::function<std::vector<BindingAsset>()> bindingAssets);

    [[nodiscard]] std::string_view GetTitle() const override { return "Vocabulary"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    [[nodiscard]] int GetDockTabGroup() const override { return 0; }
    [[nodiscard]] PanelPersistence GetPersistence() const override
    {
        return { "vocabulary", PanelVisibilityPolicy::Remembered };
    }
    void OnDraw() override;

private:
    const VocabularyCatalog& Catalog;
    std::function<std::vector<BindingAsset>()> BindingAssets;
    std::vector<BindingAsset> Assets;
    bool Scanned = false;
};
