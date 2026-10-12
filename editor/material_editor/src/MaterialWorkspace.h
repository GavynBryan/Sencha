#pragma once

#include "thumbnails/AssetPictures.h"

#include "MaterialDocumentSet.h"
#include "MaterialProperties.h"
#include "TextureImportProperties.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "presenters/StageOrbit.h"
#include "project/TextureImportStore.h"
#include "prompts/DocumentPrompts.h"
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
class ProblemList;
class ProjectFileSteps;
struct ProjectDescriptor;
struct RuntimeAssets;

// Ctrl+N: a new material, opened.
[[nodiscard]] ActionDefinition NewMaterialAction();

// The materials workspace: the open material turned on a lit stage that fills
// the window, its parameters on the card, and every open material on the
// document dial; materials and textures are found on the Shelf. Each open
// material's working description is pushed into its resident material after
// every change, so every view of it is live.
class MaterialWorkspace final : public IWorkspace
{
public:
    MaterialWorkspace(Engine& engine,
                      const ProjectDescriptor* project,
                      MaterialLibrary& materials,
                      DocumentSourceSet& documents,
                      ProjectFileSteps* files,
                      PromptQueue& prompts,
                      ActionCatalog& catalog,
                      ProblemList& problems,
                      AssetPictures& pictures);
    ~MaterialWorkspace() override;

    MaterialWorkspace(const MaterialWorkspace&) = delete;
    MaterialWorkspace& operator=(const MaterialWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    void ShowDocument(std::string_view key) override;
    bool OpenAsset(const ProjectFile& file) override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
private:
    void BuildUi();
    void DefineActions(ActionCatalog& catalog);
    // A name under materials/ that no file has yet, from `stem`.
    [[nodiscard]] std::string UnusedMaterialName(const std::string& stem) const;

    // Browser/tab actions. New writes materials/<name>.smat under the first
    // content root, re-registers it, and opens it.
    bool OpenMaterial(const std::string& virtualPath);
    void CloseTab(std::size_t index);
    void SaveActiveMaterial();
    void CreateMaterial(const std::string& name);
    // The active material's file copied beside it, as saved, and opened.
    void DuplicateActive();
    void RescanMaterials();
    void PickTexture(const std::string& texturePath);
    // The card goes back to the open material.
    void ShowMaterial();

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
    // What the last operation could not do, or nothing, as the workspace's problems.
    void Report(std::string error);

    Engine& EngineRef;
    const ProjectDescriptor* Project = nullptr;
    RuntimeAssets& Assets;
    MaterialLibrary& Materials;
    DocumentSourceSet& Documents;
    ProjectFileSteps* Files;
    MaterialDocumentSet Tabs;
    PromptQueue::Asker Prompts;
    ProblemList& Problems;
    std::string SetupProblem;

    MaterialPreviewRenderFeature* Preview = nullptr;
    ActionContext ActionHandlers;
    MaterialProperties Properties;
    TextureImportProperties TextureImport;
    // What the card is about: the open material, or a texture picked in the
    // texture explorer. Only PickTexture and ShowMaterial change it.
    enum class CardSubject : std::uint8_t
    {
        Material,
        Texture,
    };
    CardSubject Subject = CardSubject::Material;
    // The open material's file, as the one property naming where it lives.
    std::unique_ptr<IPropertySource> FilePath;
    std::unique_ptr<ContentPicture> Picture;
    // Pictures of materials and textures, shared with every workspace.
    AssetPictures& Pictures;
    Rect2d ContentRect{};
    bool Visible = false;
    StageOrbitDrag Orbit;
};
