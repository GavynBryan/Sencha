#pragma once

#include <string>
#include <vector>

// One document a workspace has open, named by the key it takes back.
struct WorkspaceOpenDocument
{
    std::string Key;
    std::string Label;
    bool Changed = false;
};

// The document the workspace is editing right now, and, for a workspace that
// keeps several open, all of them.
struct WorkspaceDocumentFacet
{
    std::string Label;
    bool Changed = false;
    std::vector<WorkspaceOpenDocument> Open = {};
    // The key of the one being edited, among Open.
    std::string Active = {};
};
