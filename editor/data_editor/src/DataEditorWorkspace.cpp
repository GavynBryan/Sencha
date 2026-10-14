#include "DataEditorWorkspace.h"

#include "project/Project.h"

#include <algorithm>
#include <vector>

namespace
{
    std::filesystem::path FirstContentRoot(const ProjectDescriptor& project)
    {
        return project.ContentRoots.empty() ? std::filesystem::path{}
                                            : std::filesystem::path(project.ContentRoots.front());
    }
}

DataEditorWorkspace::DataEditorWorkspace(RuntimeAssets& assets, const ProjectDescriptor& project,
                                         DocumentSourceSet& sources, DataDocumentStore& store)
    : Assets(assets)
    , ContentRoot(FirstContentRoot(project))
    , Sources(sources)
    , Documents(store, { .ContentRoot = ContentRoot, .Subtypes = {} })
{
}

std::vector<const AssetRecord*> DataEditorWorkspace::DataAssets() const
{
    std::vector<const AssetRecord*> records;
    for (const auto& [path, record] : Assets.Registry.Records())
        if (record.Type == AssetType::Data)
            records.push_back(&record);
    std::ranges::sort(records, {}, &AssetRecord::Path);
    return records;
}
