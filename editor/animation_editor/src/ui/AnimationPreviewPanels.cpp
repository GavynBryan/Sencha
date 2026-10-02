#include "AnimationPreviewPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationLabPanels.h"
#include "ui/AnimationEventPanels.h"
#include "ui/AnimationMigrationPanels.h"
#include "ui/AnimationScenarioBatchPanels.h"
#include "ui/AnimationSimulationPanels.h"
#include "ui/WorkspaceView.h"

#include <memory>

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    AddAnimationSimulationPanels(ui, workspace);
    AddAnimationEventPanels(ui, workspace);
    AddAnimationLabPanels(ui, workspace.Lab, workspace.Rig);
    AddAnimationMigrationPanels(ui, workspace);
    AddAnimationScenarioBatchPanels(ui, workspace);
}
