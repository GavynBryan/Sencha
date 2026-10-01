#pragma once

#include "workspaces/TreeOutline.h"
#include "workspaces/WorkspaceFacets.h"

#include <string_view>

class DocumentLibrary;

// The document library as an outline: each library a branch, its documents
// under it keyed by the package path that opens them.
class LibraryOutline
{
public:
    [[nodiscard]] ExplorerPage Explore(const DocumentLibrary& library, std::string_view openPackage,
                                       const ExplorerQuery& query);
    // Opens or closes the library `key` names; false when it names a document.
    bool Flip(std::string_view key);

private:
    TreeOutline Outline;
};
