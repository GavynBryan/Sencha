#pragma once

#include "project/TextureImportStore.h"
#include "ui/IEditorPanel.h"

#include <functional>
#include <string>
#include <vector>

class AssetRegistry;

//=============================================================================
// TexturesPanel: the texture import surface. Browses the project's source
// textures, edits the selected texture's import settings inline, and always
// shows the cooked artifact's ground truth (read from the .stex header) plus
// the result of the last apply, so a setting that failed to take is visible
// instead of silent.
//
// File-level work (sidecar IO, cooked-state reads) goes through the shared
// TextureImportStore; only the recook + resident hot-swap is the composition
// root's (it owns the reload machinery).
//=============================================================================
class TexturesPanel final : public IEditorPanel
{
public:
    using RecookFn = std::function<bool(const TextureSourceLocation& source, std::string* error)>;

    TexturesPanel(const AssetRegistry& registry,
                  std::vector<std::string> contentRoots,
                  RecookFn recook);

    [[nodiscard]] std::string_view GetTitle() const override { return "Textures"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override;

    // Selects (and reveals) a texture; the inspector's texture slots route
    // their "Import Settings..." here.
    void SelectTexture(const std::string& virtualPath);

private:
    void DrawTextureList();
    void DrawRow(const char* label, const std::string& virtualPath);
    void DrawDetails();
    void ApplyDraft();

    const AssetRegistry& Registry;
    std::vector<std::string> ContentRoots;
    RecookFn Recook;

    char FilterText[128] = "";

    // Selection + edit state.
    std::string Selected; // virtual path; empty = nothing selected
    TextureImportSettings Draft;
    bool SourceMissing = false;
    std::string Status; // last apply / load result, shown under the editor
};
