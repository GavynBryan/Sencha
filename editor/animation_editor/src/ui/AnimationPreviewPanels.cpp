#include "AnimationPreviewPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationLabPanels.h"
#include "ui/AnimationEventPanels.h"
#include "ui/AnimationMigrationPanels.h"
#include "ui/AnimationScenarioBatchPanels.h"
#include "ui/AnimationRequestSchemaPanel.h"
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
    auto requestSchema = std::make_unique<AnimationRequestSchemaPanel>(workspace.Documents, workspace.Sources);
    auto* requestSchemaPanel = requestSchema.get();
    ui.AddPanel(std::move(requestSchema));
    // Hidden panels do not receive OnDraw, including when hidden via View.
    ui.Overlays.push_back([&workspace, requestSchemaPanel] {
        if (!requestSchemaPanel->IsVisible()) workspace.Sources.CancelEdits();
    });
}
