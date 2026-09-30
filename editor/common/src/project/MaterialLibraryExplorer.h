#pragma once

#include "actions/ActionCatalog.h"
#include "workspaces/WorkspaceFacets.h"

#include <functional>
#include <string>
#include <string_view>

class MaterialLibrary;

// The project's material library as an explorer, the same in every workspace
// that browses it: what the explorer is called and opened by, its rescan, and
// a page of the materials whose name holds the query's filter, the `current`
// one marked and each pictured by `picture` when there is one. What choosing
// a material does is the workspace's.
inline constexpr std::string_view kMaterialExplorerId = "materials";

[[nodiscard]] WorkspaceExplorerInfo MaterialExplorerInfo();
[[nodiscard]] ActionDefinition MaterialExplorerAction();
[[nodiscard]] ActionDefinition MaterialRescanAction();
[[nodiscard]] ExplorerPage MaterialLibraryPage(const MaterialLibrary& materials, const ExplorerQuery& query,
                                               std::string_view current,
                                               const std::function<std::string(const std::string&)>& picture);
