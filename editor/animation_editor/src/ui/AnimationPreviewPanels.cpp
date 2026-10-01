#include "AnimationPreviewPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/AnimationBlendPanels.h"
#include "ui/AnimationLabPanels.h"
#include "ui/AnimationEventPanels.h"
#include "ui/AnimationLayerPanels.h"
#include "ui/AnimationDocumentPanels.h"
#include "ui/AnimationMigrationPanels.h"
#include "ui/AnimationScenarioBatchPanels.h"
#include "ui/AnimationRequestSchemaPanel.h"
#include "ui/AnimationRootMotionPanels.h"
#include "ui/AnimationSelectionPanels.h"
#include "ui/AnimationSimulationPanels.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimBlendOverrides.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimFlowData.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>

#include <imgui.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace
{
class PreviewAssetsPanel final : public IEditorPanel
{
public:
    explicit PreviewAssetsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Preview content"; }
    PanelPersistence GetPersistence() const override { return { "animation.content" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Left; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;
        ImGui::TextWrapped("Preview selections are transient. Animation documents open for editing; valid edits reach the running preview at once and the file only when saved.");
        if (ImGui::Button("Refresh asset list")) Workspace.RefreshBrowser();
        if (ImGui::CollapsingHeader("Animation documents", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const std::pair<const char*, std::string_view> kinds[] = {
                { "Selectors", kAnimSelectorType },
                { "Behavior sets", kAnimBehaviorSetType },
                { "Slot maps", kAnimSlotMapType },
                { "Flows", kAnimFlowType },
                { "Blendspaces", kAnimBlendspaceType },
                { "Blend overrides", kAnimBlendOverridesType },
                { "Fact schemas", kAnimFactSchemaType },
                { "Request schemas", kAnimRequestSchemaType },
                { "Rigs (as documents)", kAnimRigType },
            };
            for (const auto& [title, subtype] : kinds)
            {
                const std::span<const std::string> paths = Workspace.Content.OfSubtype(subtype);
                if (paths.empty() || !ImGui::TreeNode(title))
                    continue;
                for (const auto& path : paths)
                    if (ImGui::Selectable(path.c_str())) (void)Workspace.Documents.OpenOrFocus(path, Workspace.DocumentError);
                ImGui::TreePop();
            }
        }
        AnimationAuditionSelection& audition = Workspace.Audition;
        DrawAssets("Skinned meshes", Workspace.Content.Of(AssetType::SkinnedMesh), audition.MeshPath,
                   [&](const std::string& path) { (void)audition.SelectMesh(path); });
        DrawAssets("Skeletons (without mesh)", Workspace.Content.Of(AssetType::Skeleton), audition.Session.SkeletonPath(),
                   [&](const std::string& path) { (void)audition.SelectSkeleton(path); });
        if (ImGui::Button("Bind pose")) Workspace.AuditionClip({});
        DrawAssets("Clips", Workspace.Content.Of(AssetType::AnimationClip), audition.ClipPath,
                   [&](const std::string& path) { (void)Workspace.AuditionClip(path); });
        if (ImGui::Button("Neutral preview material")) audition.SelectMaterial({});
        DrawAssets("Material override", Workspace.Content.Of(AssetType::Material), audition.MaterialPath,
                   [&](const std::string& path) { (void)audition.SelectMaterial(path); });
    }
private:
    void DrawAssets(const char* title, std::span<const std::string> paths,
                    const std::string& selected, const std::function<void(const std::string&)>& select)
    {
        if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::PushID(title);
        for (const auto& path : paths)
        {
            if (ImGui::Selectable(path.c_str(), path == selected)) select(path);
        }
        if (paths.empty()) ImGui::TextDisabled("No assets of this kind in the mounted project.");
        ImGui::PopID();
    }
    AnimationPreviewWorkspace& Workspace;
};

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

class PreviewDetailsPanel final : public IEditorPanel
{
public:
    explicit PreviewDetailsPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Preview diagnostics"; }
    PanelPersistence GetPersistence() const override { return { "animation.diagnostics" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;
        if (!Workspace.Audition.Error.empty())
        {
            ImGui::TextWrapped("Selection rejected: %s", Workspace.Audition.Error.c_str());
            ImGui::TextWrapped("The previous valid content remains selected.");
        }
        ImGui::TextWrapped("Mesh: %s", Workspace.Audition.MeshPath.c_str());
        ImGui::TextWrapped("Skeleton: %s", Workspace.Audition.Session.SkeletonPath().c_str());
        ImGui::TextWrapped("Clip: %s", Workspace.Audition.ClipPath.empty() ? "Bind pose" : Workspace.Audition.ClipPath.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("This surface auditions cooked clips; auditioning never crosses an event mark. Rigs simulate under a scenario in the Simulation panels, and clip events are authored in Clip events.");
        const auto& joints = Workspace.Audition.Session.Skeleton().Joints;
        if (ImGui::CollapsingHeader("Skeleton hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (std::size_t i = 0; i < joints.size(); ++i)
                ImGui::Text("%zu: %s (parent %d)", i,
                            joints[i].Name.empty() ? "<unnamed>" : joints[i].Name.c_str(),
                            joints[i].ParentIndex);
        }
    }
private:
    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationPreviewPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace, PromptQueue& prompts)
{
    ui.AddPanel(std::make_unique<PreviewAssetsPanel>(workspace));
    ui.AddPanel(std::make_unique<PreviewTransportPanel>(workspace.Audition.Session));
    ui.AddPanel(std::make_unique<PreviewDetailsPanel>(workspace));
    AddAnimationSimulationPanels(ui, workspace);
    AddAnimationSelectionPanels(ui, workspace);
    AddAnimationEventPanels(ui, workspace);
    AddAnimationLayerPanels(ui, workspace);
    AddAnimationBlendPanels(ui, workspace.Rig, workspace.Takes, workspace.Viewport);
    AddAnimationLabPanels(ui, workspace.Lab, workspace.Rig);
    AddAnimationRootMotionPanels(ui, workspace.Rig, workspace.Clips());
    AddAnimationDocumentPanels(ui, workspace.Documents, workspace.Sources, prompts);
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
