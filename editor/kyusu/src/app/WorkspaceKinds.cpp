#include "app/WorkspaceKinds.h"

#include "app/LevelWorkspace.h"
#include "app/ProjectSession.h"
#include "AnimationWorkspace.h"
#include "DataWorkspace.h"
#include "MaterialWorkspace.h"
#include "ProjectWorkspace.h"
#include "SubtypeEditors.h"
#include "UiPreviewWorkspace.h"
#include "authoring/AnimationPredicateAuthoring.h"

#include <memory>

std::vector<WorkspaceKind> BuildWorkspaceKinds(Engine& engine, SdlWindow& window, ProjectSession& session,
                                               ActionCatalog& actions, PromptQueue& prompts, ProblemList& problems,
                                               NoticeQueue& notices)
{
    std::vector<WorkspaceKind> kinds;
    // Every workspace that shows data documents reads them the same way.
    const DataSubtypeAuthoringSetup authorings = [](DataSubtypeAuthoringRegistry& registry) {
        RegisterBuiltInSubtypeAuthoring(registry);
        RegisterAnimationSubtypeAuthoring(registry);
    };
    kinds.push_back(WorkspaceKind{
        .Id = "level",
        .DisplayName = "Level",
        // A level can be authored without a project; its materials then come
        // from beside the level file.
        .RequiresProject = false,
        .Create = [&engine, &window, &session, &actions, &prompts, &problems] {
            return std::make_unique<LevelWorkspace>(engine, window, session.Project(), session.Module(),
                                                    session.Materials(), session.Documents(), actions, prompts,
                                                    problems, session.Pictures());
        },
        .Opens = { { ".sscene" }, { ".sworld" } },
        .Creates = { NewLevelAction(), NewWorldAction() },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "materials",
        .DisplayName = "Materials",
        .Create = [&engine, &session, &prompts, &actions] {
            return std::make_unique<MaterialWorkspace>(engine, session.Project(), session.Materials(),
                                                       session.Documents(), session.Files(), prompts, actions,
                                                       session.Pictures());
        },
        .Opens = { { ".smat" }, { ".png" } },
        .Creates = { NewMaterialAction() },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "data",
        .DisplayName = "Data",
        .Create = [&engine, &session, &prompts, &actions, &problems, authorings] {
            return std::make_unique<DataWorkspace>(engine, *session.Project(), session.Documents(),
                                                   session.DataDocuments(), session.Files(), prompts, actions,
                                                   problems, authorings);
        },
        .Opens = { { ".sdata" } },
        .Creates = { NewDataAssetAction() },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "animation",
        .DisplayName = "Animation",
        .Create = [&engine, &session, &prompts, &actions, &problems, authorings] {
            return std::make_unique<AnimationWorkspace>(engine, *session.Project(), session.Module(),
                                                        session.Documents(), session.DataDocuments(), prompts,
                                                        actions, problems, authorings);
        },
        .Opens = { { ".sdata", "animation.rig" } },
        .Creates = { NewRigAction() },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "ui_preview",
        .DisplayName = "UI Preview",
        // The engine's and the editor's own documents are there without one.
        .RequiresProject = false,
        .Create = [&engine, &session, &actions, &problems, &notices] {
            return std::make_unique<UiPreviewWorkspace>(engine, session.Project(), session.Module(), actions,
                                                        problems, notices);
        },
        .Opens = { { ".rml" } },
    });
    kinds.push_back(WorkspaceKind{
        .Id = "project",
        .DisplayName = "Project",
        .RequiresProject = false,
        .Create = [&engine, &window, &actions] {
            return std::make_unique<ProjectWorkspace>(engine, window, actions, "kyusu");
        },
    });
    return kinds;
}
