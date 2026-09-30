#include "project/ProjectContentMount.h"

#include "project/Project.h"

#include <app/EngineContentRoot.h>
#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <string>

namespace
{
// The runtime's resolution of one root: authored scan, sources cooked on demand
// (hash-gated, so an unchanged source costs a lookup), then the cooked overlay,
// which wins. The cook is what makes the Solid viewport WYSIWYG.
void MountSourceRoot(const std::string& root, RuntimeAssets& assets, LoggingProvider& logging,
                     JobSystem* jobs, Logger& log)
{
    const ContentRootPaths paths = ResolveContentRoot(root);
    ScanContentRoot(paths, assets);
    {
        ContentImporterSet importers(jobs);
        (void)ImportAssetsOnDemand(root, importers.Registry(), assets.Registry, logging);
    }
    RegisterCookedContent(paths, assets, log);
}
} // namespace

void MountProjectContent(const ProjectDescriptor& project,
                         RuntimeAssets& assets,
                         LoggingProvider& logging,
                         JobSystem* jobs)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    for (const std::string& root : project.ContentRoots)
        MountSourceRoot(root, assets, logging, jobs, log);
    log.Info("assets: mounted {} content root(s)", project.ContentRoots.size());
}

void MountEngineContent(RuntimeAssets& assets, LoggingProvider& logging, JobSystem* jobs)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    const std::filesystem::path root = EngineContentRoot();
    if (root.empty())
        return;
    MountSourceRoot(root.string(), assets, logging, jobs, log);
    log.Info("assets: mounted engine content at '{}'", root.string());
}

void MountEditorContent(std::string_view root,
                        RuntimeAssets& assets,
                        LoggingProvider& logging,
                        JobSystem* jobs)
{
    Logger& log = logging.GetLogger<ProjectDescriptor>();
    const std::string rootPath(root);
    MountSourceRoot(rootPath, assets, logging, jobs, log);
    log.Info("assets: mounted editor UI content at '{}'", rootPath);
}
