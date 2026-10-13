#pragma once

#include <string>
#include <vector>

// A resource as shown: its name, and the host image that pictures it. Both are
// empty for an empty slot; the image alone is empty when nothing pictures it.
struct HeldResource
{
    std::string Label = {};
    std::string Image = {};
    // The action choosing it performs; empty when choosing it does nothing.
    std::string Action = {};
};

// What the workspace's operations consume, per role: the resource the next one
// will use, and those stashed beside it.
struct WorkspaceResourceFacet
{
    std::string Role = {};
    bool Primary = false;
    // The explorer that browses for another, or empty.
    std::string Explorer = {};
    HeldResource Active = {};
    std::vector<HeldResource> Stash = {};
};
