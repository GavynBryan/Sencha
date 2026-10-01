#include "authoring/LibraryOutline.h"

#include "authoring/DocumentLibrary.h"

#include "text/TextFilterMatch.h"

namespace
{
constexpr std::string_view kLibraryKey = "library:";
}

ExplorerPage LibraryOutline::Explore(const DocumentLibrary& library, std::string_view openPackage,
                                     const ExplorerQuery& query) const
{
    const bool filtering = !query.Filter.empty();
    const std::vector<DocumentEntry>& entries = library.Documents();
    ExplorerPage page;
    page.Actions = { "ui_preview.rescan" };
    page.Empty = entries.empty() ? "No documents. Open a project, or add .rml files to a mounted root."
                                 : "No document's path holds that.";
    for (std::size_t first = 0; first < entries.size();)
    {
        const std::string& name = entries[first].Library;
        std::size_t end = first;
        bool any = false;
        while (end < entries.size() && entries[end].Library == name)
            any |= TextFilterMatch(entries[end++].RelPath, query.Filter);
        if (any)
        {
            const bool open = filtering || !Closed.contains(name);
            if (CountOnPage(page, query))
                page.Items.push_back({ .Key = std::string(kLibraryKey) + name,
                                       .Label = name,
                                       .Branch = open ? ExplorerBranch::Open : ExplorerBranch::Closed });
            for (std::size_t i = first; open && i < end; ++i)
            {
                const DocumentEntry& entry = entries[i];
                if (!TextFilterMatch(entry.RelPath, query.Filter) || !CountOnPage(page, query))
                    continue;
                page.Items.push_back({ .Key = entry.PackagePath,
                                       .Label = entry.Cooked ? entry.RelPath : entry.RelPath + "  (not cooked)",
                                       .Depth = 1,
                                       .Current = entry.PackagePath == openPackage });
            }
        }
        first = end;
    }
    return page;
}

bool LibraryOutline::Flip(std::string_view key)
{
    if (!key.starts_with(kLibraryKey))
        return false;
    const std::string name(key.substr(kLibraryKey.size()));
    if (!Closed.erase(name))
        Closed.insert(name);
    return true;
}
