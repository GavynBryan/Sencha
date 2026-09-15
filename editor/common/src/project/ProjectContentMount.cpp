#include "project/ProjectContentMount.h"

#include "project/Project.h"

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <string>

void MountProjectContent(const ProjectDescriptor& project,
                         RuntimeAssets& assets,
                         LoggingProvider& logging,
                         JobSystem* jobs)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    for (const std::string& root : project.ContentRoots)
    {
        const ContentRootPaths paths = ResolveContentRoot(root);
        ScanContentRoot(paths, assets);

        // Cook source assets on demand and register the cooked overlay, so a
        // material's asset://...png resolves to its cooked .stex with a bindless
        // slot and a placed asset://...blend resolves to its cooked mesh: the same
        // resolve the runtime uses, which is what makes the Solid viewport
        // WYSIWYG. Editors are cook-enabled; cooked wins over the scan.
        //
        // Meshes belong here for the same reason textures do. Without them the
        // .glb and .blend import paths exist, are tested, and are reachable from
        // nothing -- a source mesh dropped into a project never becomes an asset,
        // so it can never be placed. The work is hash-gated by the cooked-cache
        // index, so an unchanged source costs a hash and a lookup.
        {
            ContentImporterSet importers(jobs);
            (void)ImportAssetsOnDemand(root, importers.Registry(), assets.Registry, logging);
        }
        RegisterCookedContent(paths, assets, log);
    }
    log.Info("assets: mounted {} content root(s)", project.ContentRoots.size());
}

void MountEditorContent(std::string_view root,
                        RuntimeAssets& assets,
                        LoggingProvider& logging,
                        JobSystem* jobs)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    const std::string rootPath(root);

    const ContentRootPaths paths = ResolveContentRoot(rootPath);
    ScanContentRoot(paths, assets);
    {
        ContentImporterSet importers(jobs);
        (void)ImportAssetsOnDemand(rootPath, importers.Registry(), assets.Registry, logging);
    }
    RegisterCookedContent(paths, assets, log);
    log.Info("assets: mounted editor UI content at '{}'", rootPath);
}
