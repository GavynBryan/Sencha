#pragma once

#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"
#include "project/MaterialLibrary.h"
#include "project/AssetAdoption.h"
#include "project/Project.h"
#include "project/ReferenceIndex.h"
#include "thumbnails/AssetPictures.h"

#include <app/GameModuleLoader.h>
#include <assets/cook/ContentImporters.h>

#include <memory>
#include <span>
#include <optional>
#include <string>

class Engine;
class Game;
class MaterialThumbnailCache;
class ProjectFileSteps;
struct ProjectFileChange;
struct RuntimeAssets;

// The project a Kyusu process edits, held once for every workspace. Built
// before the workspaces and destroyed after them, so the module stays mapped
// while anything it compiled is alive.
class ProjectSession
{
public:
    // projectPath is the resolved .senchaproj (ResolveProjectPath), if any.
    // Without one, SENCHA_GAME_MODULE may still name a bare game module.
    ProjectSession(Engine& engine, std::optional<std::string> projectPath);
    ~ProjectSession();

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;

    // Mutable: cook profiles are edited into the descriptor and saved with it.
    [[nodiscard]] ProjectDescriptor* Project() { return Descriptor ? &*Descriptor : nullptr; }
    [[nodiscard]] Game* Module() const { return GameModule.Instance; }
    [[nodiscard]] RuntimeAssets& Assets() const;
    [[nodiscard]] MaterialLibrary& Materials() { return MaterialList; }
    // The one journal and save-all across every workspace's documents.
    [[nodiscard]] DocumentSourceSet& Documents() { return Journal; }
    // One document per data file, however many workspaces show it.
    [[nodiscard]] DataDocumentStore& DataDocuments() { return DataDocumentFiles; }
    // Pictures of content files, for every workspace and the shelf: materials
    // and textures from one texture cache, other kinds as workspaces provide.
    [[nodiscard]] AssetPictures& Pictures() { return PictureSources; }
    // Once a frame: makes the material and texture pictures once authored UI
    // is ready, and keeps how many are resident within budget.
    void UpdatePictures();
    // Which of the project's files name which, as last read by a shelf.
    [[nodiscard]] ReferenceIndex& References() { return ContentReferences; }
    // What opening the project did to leave every file stating its own id,
    // and what it could not do.
    [[nodiscard]] const AssetAdoption& Adoption() const { return Adoption_; }
    [[nodiscard]] std::span<const std::string> AdoptionErrors() const { return AdoptionErrors_; }
    // Whether `file` is an asset, and so goes by an id (IsAssetFile).
    [[nodiscard]] bool IsAsset(const ProjectFile& file) const;
    // Moving, copying and trashing the project's files as journal steps; null
    // without a project.
    [[nodiscard]] ProjectFileSteps* Files() { return FileSteps.get(); }

private:
    void LoadModule(const std::string& modulePath);
    void MountContent();
    void WatchSources();
    // A file step moved, copied or trashed a file: the asset stack and every
    // view of the project's files are told.
    void FileChanged(const ProjectFileChange& change);

    Engine& Host;
    std::optional<ProjectDescriptor> Descriptor;
    ContentImporterSet Importers;
    GameModuleLoader ModuleLoader;
    LoadedModule GameModule;
    MaterialLibrary MaterialList;
    DocumentSourceSet Journal;
    DataDocumentStore DataDocumentFiles;
    AssetPictures PictureSources;
    ReferenceIndex ContentReferences;
    AssetAdoption Adoption_;
    std::vector<std::string> AdoptionErrors_;
    std::unique_ptr<MaterialThumbnailCache> MaterialPictures;
    AssetPictures::Registration MaterialPictureFiles;
    AssetPictures::Registration TexturePictureFiles;
    // Last: its steps reach the journal, the asset stack and the pictures.
    std::unique_ptr<ProjectFileSteps> FileSteps;
};
