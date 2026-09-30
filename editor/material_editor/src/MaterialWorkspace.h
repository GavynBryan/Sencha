#pragma once

#include "MaterialDocumentSet.h"
#include "MaterialProperties.h"

#include "actions/ActionContext.h"
#include "project/TextureImportStore.h"
#include "prompts/DocumentPrompts.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstddef>
#include <memory>
#include <string>

class ActionCatalog;
class ContentPicture;
class DocumentSourceSet;
class IPropertySource;
class Engine;
class MaterialLibrary;
class MaterialThumbnailCache;
class MaterialPreviewRenderFeature;
class TexturesPanel;
struct ProjectDescriptor;
struct RuntimeAssets;

// The materials workspace: the open material turned on a lit stage that fills
// the window, its parameters on the card, the project's materials in an
// explorer, and every open material on the document dial. Each open
// material's working description is pushed into its resident material after
// every change, so every view of it is live.
class MaterialWorkspace final : public IWorkspace
{
public:
    MaterialWorkspace(Engine& engine,
                      const ProjectDescriptor* project,
                      MaterialLibrary& materials,
                      DocumentSourceSet& documents,
                      PromptQueue& prompts,
                      ActionCatalog& catalog);
    ~MaterialWorkspace() override;

    MaterialWorkspace(const MaterialWorkspace&) = delete;
    MaterialWorkspace& operator=(const MaterialWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool HoldsPointer() const override { return Orbiting; }
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    void ShowDocument(std::string_view key) override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    WorkspaceView& View() override { return Surface; }

private:
    void BuildUi();
    void DefineActions(ActionCatalog& catalog);
    // A name under materials/ that no file has yet, from `stem`.
    [[nodiscard]] std::string UnusedMaterialName(const std::string& stem) const;

    // Browser/tab actions. New/Duplicate write materials/<name>.smat under the
    // first content root, re-register it, and open it.
    void OpenMaterial(const std::string& virtualPath);
    void CloseTab(std::size_t index);
    void SaveActiveMaterial();
    void CreateMaterial(const std::string& name, bool duplicateOpen);
    // Moves the .smat on disk to content-root-relative newRelPath (".smat"
    // appended when missing) and re-points any open tab. Refs in levels are
    // not rewritten; they fall back to the level default until reassigned.
    void RenameMaterial(const std::string& virtualPath, const std::string& newRelPath);
    void RescanMaterials();

    // Recooks a texture source and swaps the resident texture in place so the
    // preview (and every material sampling it) shows the new cook at once.
    bool RecookTexture(const TextureSourceLocation& source, std::string* error);

    // Writes a new .smat beside `textureVirtualPath`, named after it with a
    // "T-" prefix swapped to "M-" and the texture bound as base color, and
    // opens it. An existing material at that path just opens.
    void CreateMaterialFromTexture(const std::string& textureVirtualPath);

    // editor.preview.backdrop.*, read into the render feature every frame so
    // they tune live from the dev console.
    void RegisterPreviewBackdropCVars();
    void UpdatePreviewBackdropStyle();

    // Pushes a description into a tab's resident material (in-place swap;
    // live handles keep working).
    void PushToResident(MaterialEditTab& tab, const MaterialDescription& description);

    Engine& EngineRef;
    const ProjectDescriptor* Project = nullptr;
    RuntimeAssets& Assets;
    MaterialLibrary& Materials;
    DocumentSourceSet& Documents;
    MaterialDocumentSet Tabs;
    PromptQueue::Asker Prompts;

    MaterialPreviewRenderFeature* Preview = nullptr;
    TexturesPanel* Textures = nullptr;
    ActionContext ActionHandlers;
    MaterialProperties Properties;
    // The open material's file, as the one property naming where it lives.
    std::unique_ptr<IPropertySource> FilePath;
    std::unique_ptr<ContentPicture> Picture;
    // Pictures for the material explorer; its own, so its host images are too.
    std::unique_ptr<MaterialThumbnailCache> Thumbnails;
    Rect2d ContentRect{};
    bool Visible = false;
    // A drag on the stage turns the material over until the button lets go.
    bool Orbiting = false;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
