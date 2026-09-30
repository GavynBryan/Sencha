#pragma once

#include "workspaces/WorkspaceKind.h"

#include <vector>

class ActionCatalog;
class Engine;
class ProjectSession;
class SdlWindow;

// The workspaces Kyusu offers, in the order its menus list them. Each factory
// hands its workspace exactly the services it needs.
[[nodiscard]] std::vector<WorkspaceKind> BuildWorkspaceKinds(Engine& engine, SdlWindow& window,
                                                             ProjectSession& session,
                                                             ActionCatalog& actions);
