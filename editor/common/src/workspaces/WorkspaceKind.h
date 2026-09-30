#pragma once

#include <functional>
#include <memory>
#include <string>

class IWorkspace;

// One row of the table of workspaces an application offers: what it is called
// and how to build one. The table is data; opening a kind runs its factory.
struct WorkspaceKind
{
    // Stable identity: console commands, settings scopes and window names use it.
    std::string Id;
    // What a designer sees on the tab.
    std::string DisplayName;
    // A kind that edits project content is not offered without a project.
    bool RequiresProject = true;
    std::function<std::unique_ptr<IWorkspace>()> Create;
};
