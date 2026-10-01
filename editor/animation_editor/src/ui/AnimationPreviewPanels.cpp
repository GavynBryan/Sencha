#include "AnimationPreviewPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationBlendPanels.h"
#include "ui/AnimationLabPanels.h"
#include "ui/AnimationEventPanels.h"
#include "ui/AnimationLayerPanels.h"
#include "ui/AnimationMigrationPanels.h"
#include "ui/AnimationScenarioBatchPanels.h"
#include "ui/AnimationRequestSchemaPanel.h"
#include "ui/AnimationRootMotionPanels.h"
#include "ui/AnimationSelectionPanels.h"
#include "ui/AnimationSimulationPanels.h"
#include "ui/WorkspaceView.h"

#include <memory>

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    AddAnimationSimulationPanels(ui, workspace);
    AddAnimationSelectionPanels(ui, workspace);
    AddAnimationEventPanels(ui, workspace);
    AddAnimationLayerPanels(ui, workspace);
    AddAnimationBlendPanels(ui, workspace.Rig, workspace.Takes, workspace.Viewport);
    AddAnimationLabPanels(ui, workspace.Lab, workspace.Rig);
    AddAnimationRootMotionPanels(ui, workspace.Rig, workspace.Clips());
    AddAnimationMigrationPanels(ui, workspace);
    AddAnimationScenarioBatchPanels(ui, workspace);
    auto requestSchema = std::make_unique<AnimationRequestSchemaPanel>(workspace.Documents, workspace.Sources);
    auto* requestSchemaPanel = requestSchema.get();
    ui.AddPanel(std::move(requestSchema));
    // Hidden panels do not receive OnDraw, including when hidden via View.
    ui.Overlays.push_back([&workspace, requestSchemaPanel] {
        if (!requestSchemaPanel->IsVisible()) workspace.Sources.CancelEdits();
    });
}
