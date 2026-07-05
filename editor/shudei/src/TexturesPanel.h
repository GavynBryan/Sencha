#pragma once

#include "project/TextureImportStore.h"
#include "ui/IEditorPanel.h"
#include "ui/ImGuiTextureBinding.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AssetRegistry;

//=============================================================================
// TexturesPanel: the texture import surface. Browses the project's source
// textures, edits the selected texture's import settings inline, previews the
// selected texture's cooked pixels, and always shows the cooked artifact's
// ground truth (read from the .stex header) plus the result of the last
// apply, so a setting that failed to take is visible instead of silent.
//
// File-level work (sidecar IO, cooked-state reads) goes through the shared
// TextureImportStore; only the recook + resident hot-swap and material
// creation are the composition root's (it owns that machinery).
//=============================================================================
class TexturesPanel final : public IEditorPanel
{
public:
    using RecookFn = std::function<bool(const TextureSourceLocation& source, std::string* error)>;
    using CreateMaterialFn = std::function<void(const std::string& textureVirtualPath)>;

    TexturesPanel(const AssetRegistry& registry,
                  std::vector<std::string> contentRoots,
                  RecookFn recook,
                  std::unique_ptr<ImGuiTextureBinding> preview,
                  CreateMaterialFn createMaterial);

    [[nodiscard]] std::string_view GetTitle() const override { return "Textures"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override;

    // Selects (and reveals) a texture; the inspector's texture slots route
    // their "Import Settings..." here.
    void SelectTexture(const std::string& virtualPath);

    // Drops the preview's GPU/asset references. The composition root calls
    // this in its own teardown: the panel itself is destroyed later (with the
    // UI render feature), after the asset system is gone.
    void ReleasePreviewResources();

private:
    void DrawTextureList();
    void DrawRow(const char* label, const std::string& virtualPath);
    void DrawDetails();
    // Returns whether the cooked texture samples nearest, so the preview
    // below can match its filtering.
    bool DrawImportSettings();
    void DrawPreview(bool nearestFilter);
    void ApplyDraft();

    const AssetRegistry& Registry;
    std::vector<std::string> ContentRoots;
    RecookFn Recook;
    std::unique_ptr<ImGuiTextureBinding> Preview;
    CreateMaterialFn CreateMaterial;

    char FilterText[128] = "";

    // List column's share of the panel width (splitter-adjustable).
    float ListFraction = 0.35f;

    // Selection + edit state.
    std::string Selected; // virtual path; empty = nothing selected
    TextureImportSettings Draft;
    bool SourceMissing = false;
    std::string Status; // last apply / load result, shown under the editor
};
