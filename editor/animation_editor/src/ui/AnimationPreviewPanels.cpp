#include "AnimationPreviewPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationLabPanels.h"
#include "ui/WorkspaceView.h"

#include <memory>

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    AddAnimationLabPanels(ui, workspace.Lab, workspace.Rig);
}
