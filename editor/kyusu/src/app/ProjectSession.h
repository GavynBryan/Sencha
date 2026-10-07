#pragma once

#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "project/ReferenceIndex.h"
#include "thumbnails/AssetPictures.h"

#include <app/GameModuleLoader.h>

#include <memory>
#include <optional>
#include <string>

class Engine;
class Game;
class MaterialThumbnailCache;
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

private:
    void LoadModule(const std::string& modulePath);
    void MountContent();
    void WatchSources();

    Engine& Host;
    std::optional<ProjectDescriptor> Descriptor;
    GameModuleLoader ModuleLoader;
    LoadedModule GameModule;
    MaterialLibrary MaterialList;
    DocumentSourceSet Journal;
    DataDocumentStore DataDocumentFiles;
    AssetPictures PictureSources;
    ReferenceIndex ContentReferences;
    std::unique_ptr<MaterialThumbnailCache> MaterialPictures;
    AssetPictures::Registration MaterialPictureFiles;
    AssetPictures::Registration TexturePictureFiles;
};
