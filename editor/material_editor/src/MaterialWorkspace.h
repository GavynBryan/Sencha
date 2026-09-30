#pragma once

#include "MaterialDocumentSet.h"

#include "project/TextureImportStore.h"
#include "ui/DocumentShellActions.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <cstddef>
#include <string>

class DocumentSourceSet;
class Engine;
class MaterialLibrary;
class MaterialPreviewRenderFeature;
class TexturesPanel;
struct ProjectDescriptor;
struct RuntimeAssets;

// The materials workspace: browse, inspect and preview .smat files, one tab
// per open material. Each tab's working description is pushed into its
// resident material after every change, so every view of it is live.
class MaterialWorkspace final : public IWorkspace
{
public:
    MaterialWorkspace(Engine& engine,
                      const ProjectDescriptor* project,
                      MaterialLibrary& materials,
                      DocumentSourceSet& documents);
    ~MaterialWorkspace() override;

    MaterialWorkspace(const MaterialWorkspace&) = delete;
    MaterialWorkspace& operator=(const MaterialWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    WorkspaceView& View() override { return Surface; }

private:
    void BuildUi();

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
    UnsavedDocumentPrompt ClosePrompt;

    MaterialPreviewRenderFeature* Preview = nullptr;
    TexturesPanel* Textures = nullptr;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
