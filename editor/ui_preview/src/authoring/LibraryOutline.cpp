#include "authoring/LibraryOutline.h"

#include "authoring/DocumentLibrary.h"

#include <vector>

namespace
{
constexpr std::string_view kLibraryKey = "library:";
}

ExplorerPage LibraryOutline::Explore(const DocumentLibrary& library, std::string_view openPackage,
                                     const ExplorerQuery& query)
{
    std::vector<OutlineNode> nodes;
    std::string_view current;
    for (const DocumentEntry& entry : library.Documents())
    {
        if (entry.Library != current)
        {
            current = entry.Library;
            nodes.push_back({ .Key = std::string(kLibraryKey) + entry.Library, .Label = entry.Library });
        }
        nodes.push_back({ .Key = entry.PackagePath,
                          .Label = entry.Cooked ? entry.RelPath : entry.RelPath + "  (not cooked)",
                          .Depth = 1,
                          .Current = entry.PackagePath == openPackage });
    }
    ExplorerPage page = Outline.Explore(nodes, query);
    page.Actions = { "ui_preview.rescan" };
    page.Empty = library.Documents().empty() ? "No documents. Open a project, or add .rml files to a mounted root."
                                             : "No document's path holds that.";
    return page;
}

bool LibraryOutline::Flip(std::string_view key)
{
    if (!key.starts_with(kLibraryKey))
        return false;
    Outline.Flip(key);
    return true;
}
