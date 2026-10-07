#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class IWorkspace;

// A kind of content file a workspace opens: its extension, and for a data
// asset its subtype, empty for every subtype.
struct WorkspaceOpens
{
    std::string Extension;
    std::string Subtype = {};
};

// One row of the table of workspaces an application offers: what it is called,
// how to build one, and the files it opens. The table is data; opening a kind
// runs its factory.
struct WorkspaceKind
{
    // Stable identity: console commands, settings scopes and window names use it.
    std::string Id;
    // What a designer sees on the tab.
    std::string DisplayName;
    // A kind that edits project content is not offered without a project.
    bool RequiresProject = true;
    std::function<std::unique_ptr<IWorkspace>()> Create;
    std::vector<WorkspaceOpens> Opens = {};
};
