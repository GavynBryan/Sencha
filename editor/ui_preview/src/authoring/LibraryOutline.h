#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <set>
#include <string>
#include <string_view>

class DocumentLibrary;

//=============================================================================
// LibraryOutline
//
// The document library as an outline: each library a branch, open until
// closed, its documents under it keyed by the package path that opens them.
// With a filter, a library shows the documents whose path holds it, open.
//=============================================================================
class LibraryOutline
{
public:
    [[nodiscard]] ExplorerPage Explore(const DocumentLibrary& library, std::string_view openPackage,
                                       const ExplorerQuery& query) const;
    // Opens or closes the library `key` names; false when it names a document.
    bool Flip(std::string_view key);

private:
    std::set<std::string, std::less<>> Closed;
};
