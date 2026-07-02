#include "project/ProjectContentMount.h"

#include "project/Project.h"

#include <assets/cook/AssetImporter.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/TextureCook.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/RuntimeAssets.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <string>

void MountProjectContent(const ProjectDescriptor& project,
                         RuntimeAssets& assets,
                         LoggingProvider& logging)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    for (const std::string& root : project.ContentRoots)
    {
        ScanAssetsDirectory(root, assets.Registry);
        ScanAssetsDirectory((std::filesystem::path(root) / ".cooked").string(), assets.Registry);

        // Cook source textures on demand (.png -> .stex) and register the cooked
        // overlay, so a material's asset://...png resolves to its cooked .stex with a
        // bindless slot: the same resolve the runtime uses, which is what makes the
        // Solid viewport WYSIWYG. Editors are cook-enabled; cooked wins over the scan.
        {
            PngTextureImporter textureImporter;
            AssetImporterRegistry importers;
            importers.Register(textureImporter);
            (void)ImportAssetsOnDemand(root, importers, assets.Registry, logging);
        }
        RegisterCookedAssets(root, assets.Registry);

        AssetIdMap idMap;
        std::string idMapError;
        const std::string idMapPath = (std::filesystem::path(root) / kAssetIdMapFileName).string();
        if (AssetIdMap::LoadFromFile(idMapPath, idMap, &idMapError))
            ApplyAssetIds(idMap, assets.Registry);
    }
    log.Info("assets: mounted {} content root(s)", project.ContentRoots.size());
}
