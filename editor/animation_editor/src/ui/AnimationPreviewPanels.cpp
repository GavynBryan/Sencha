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
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <memory>

namespace
{
class PreviewTransportPanel final : public IEditorPanel
{
public:
    explicit PreviewTransportPanel(AnimationClipPreviewSession& session) : Session(session) {}
    std::string_view GetTitle() const override { return "Content timeline"; }
    PanelPersistence GetPersistence() const override { return { "animation.timeline" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;
        if (ImGui::Button(Session.IsPlaying() ? "Pause" : "Play"))
        {
            if (Session.IsPlaying()) Session.Pause(); else Session.Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart")) Session.Restart();
        ImGui::SameLine();
        if (ImGui::Button("Previous tick")) Session.Step(-1);
        ImGui::SameLine();
        if (ImGui::Button("Next tick")) Session.Step(1);
        ImGui::SameLine();
        bool loop = Session.IsLooping();
        if (ImGui::Checkbox("Loop", &loop)) Session.SetLoop(loop);
        ImGui::SameLine();
        float speed = static_cast<float>(Session.Speed());
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Speed", &speed, 0.05f, 8.0f, "%.2fx"))
            (void)Session.SetSpeed(speed);
        float time = static_cast<float>(Session.NormalizedTime());
        if (ImGui::SliderFloat("Normalized content time", &time, 0.0f, 1.0f, "%.4f"))
            Session.InspectNormalized(time);
        ImGui::Text("Tick %llu at %u Hz | sample %.4f / %.4f seconds",
                    static_cast<unsigned long long>(Session.Tick()), Session.TickRate,
                    Session.SampleSeconds(), Session.Duration());
        if (Session.IsInspecting())
        {
            ImGui::TextWrapped("Inspecting a scratch pose. Playback tick is unchanged; no events dispatch.");
            if (ImGui::Button("Return to playback tick")) Session.ReturnToPlayback();
        }
    }
private:
    AnimationClipPreviewSession& Session;
};

}

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<PreviewTransportPanel>(workspace.Audition.Session));
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
