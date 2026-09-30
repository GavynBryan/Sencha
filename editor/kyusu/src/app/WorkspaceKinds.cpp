#include "app/WorkspaceKinds.h"

#include "app/LevelWorkspace.h"
#include "app/ProjectSession.h"
#include "ProjectWorkspace.h"

#include <memory>

std::vector<WorkspaceKind> BuildWorkspaceKinds(Engine& engine, SdlWindow& window, ProjectSession& session)
{
    std::vector<WorkspaceKind> kinds;
    kinds.push_back(WorkspaceKind{
        .Id = "level",
        .DisplayName = "Level",
        // A level can be authored without a project; its materials then come
        // from beside the level file.
        .RequiresProject = false,
        .Create = [&engine, &window, &session] {
            return std::make_unique<LevelWorkspace>(engine, window, session.Project(), session.Module(),
                                                    session.Materials(), session.Documents());
        },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "project",
        .DisplayName = "Project",
        .RequiresProject = false,
        .Create = [&engine, &window] { return std::make_unique<ProjectWorkspace>(engine, window, "kyusu"); },
    });
    return kinds;
}
