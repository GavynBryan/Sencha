#include "AnimationPreviewPanels.h"

#include "authoring/AnimationJointPicking.h"
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
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/EditorUiFeature.h"
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
        ScopedPanel panel(GetTitle(), &Visible);
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

class PreviewViewportPanel final : public IEditorPanel
{
public:
    PreviewViewportPanel(AnimationPreviewRenderFeature*& viewport, AnimationPreviewWorkspace& workspace)
        : Viewport(viewport)
        , Workspace(workspace)
    {
    }
    std::string_view GetTitle() const override { return "Animated rig preview"; }
    PanelPersistence GetPersistence() const override { return { "animation.viewport" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Center; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;
        if (!Viewport)
        {
            ImGui::TextWrapped("Preview renderer unavailable. See the engine log for initialization errors.");
            return;
        }
        if (ImGui::Button("Frame mesh")) Viewport->FrameSubject();
        ImGui::SameLine();
        if (Workspace.Simulation.IsOpen())
        {
            int source = static_cast<int>(Workspace.Viewport.Source);
            ImGui::RadioButton("Audition", &source, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Simulation", &source, 1);
            Workspace.Viewport.Source = static_cast<AnimationViewportSource>(source);
            ImGui::SameLine();
        }
        ImGui::Checkbox("Joints", &ShowJoints);
        ImGui::SameLine();
        if (Workspace.Simulation.SubjectTransform() != nullptr)
        {
            ImGui::Checkbox("Path", &ShowPath);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Where the character went (green), where root motion carried it past what it "
                                  "achieved (red, with a marker), and the scenario's walls.");
            ImGui::SameLine();
        }
        ImGui::TextDisabled(ShowJoints ? "Drag to orbit; click a joint to select it, right-click to mask"
                                       : "Drag to orbit; wheel to zoom");
        if (Workspace.Viewport.Source == AnimationViewportSource::Simulation && !Workspace.Viewport.Note.empty())
            ImGui::TextWrapped("%s", Workspace.Viewport.Note.c_str());
        const auto size = ImGui::GetContentRegionAvail();
        if (size.x < 8.0f || size.y < 8.0f) return;
        const auto texture = Viewport->Display({ static_cast<std::uint32_t>(size.x),
                                                static_cast<std::uint32_t>(size.y) });
        if (!texture) return;
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::Image(texture, size);
        ImGui::SetCursorScreenPos(position);
        ImGui::InvisibleButton("orbit", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) Viewport->Zoom(ImGui::GetIO().MouseWheel);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const auto delta = ImGui::GetIO().MouseDelta;
            Viewport->Orbit(delta.x * 0.01f, -delta.y * 0.01f);
        }
        if (ShowJoints)
            DrawJoints(position, size, hovered);
        if (ShowPath && Workspace.Viewport.Source == AnimationViewportSource::Simulation)
            DrawPath(position, size);
    }

private:
    void DrawPath(ImVec2 origin, ImVec2 size)
    {
        const AnimationPreviewSession& session = Workspace.Simulation;
        const Mat4 viewProjection = Viewport->ViewCamera(size.x / size.y).ViewProjection;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float feet = session.SubjectHeight() * 0.5f;
        const auto project = [&](const Vec3d& point) -> std::optional<ImVec2> {
            const std::optional<AnimationViewportPoint> at =
                ProjectAnimationViewportPoint(point, viewProjection, size.x, size.y);
            if (!at.has_value())
                return std::nullopt;
            return ImVec2(origin.x + at->X, origin.y + at->Y);
        };
        const auto line = [&](const Vec3d& a, const Vec3d& b, ImU32 colour, float width) {
            const std::optional<ImVec2> from = project(a);
            const std::optional<ImVec2> to = project(b);
            if (from && to)
                draw->AddLine(*from, *to, colour, width);
        };

        if (const AnimationScenarioMovement* movement = session.Scenario().Movement ? &*session.Scenario().Movement
                                                                                    : nullptr)
            for (const AnimationScenarioWall& wall : movement->Walls)
            {
                const Vec3d c = wall.Center;
                const Vec3d h = wall.HalfExtents;
                Vec3d corners[8];
                for (int i = 0; i < 8; ++i)
                    corners[i] = Vec3d(c.X + ((i & 1) ? h.X : -h.X), c.Y + ((i & 2) ? h.Y : -h.Y),
                                       c.Z + ((i & 4) ? h.Z : -h.Z));
                for (int i = 0; i < 8; ++i)
                    for (const int bit : { 1, 2, 4 })
                        if ((i & bit) == 0)
                            line(corners[i], corners[i | bit], IM_COL32(170, 180, 200, 200), 1.0f);
            }

        const std::optional<AnimTick> shown = ShownAnimationTick(Workspace.Simulation, Workspace.Navigation);
        Vec3d previous;
        bool first = true;
        for (const AnimationPreviewTickRecord& record : session.History())
        {
            if (!record.Movement || (shown && record.Tick > *shown))
                continue;
            const Vec3d at = record.Movement->Position - Vec3d(0.0f, feet, 0.0f);
            if (!first)
                line(previous, at, IM_COL32(90, 210, 130, 255), 2.0f);
            if (record.Movement->Blocked)
            {
                const Vec3d from = at - record.Movement->Achieved;
                line(from, from + record.Movement->Requested, IM_COL32(230, 80, 70, 255), 2.0f);
                if (const std::optional<ImVec2> mark = project(at))
                    draw->AddCircle(*mark, 4.0f, IM_COL32(230, 80, 70, 255), 0, 2.0f);
            }
            previous = at;
            first = false;
        }
    }

    // A click that did not orbit picks a joint; a right-click also offers mask steps.
    void DrawJoints(ImVec2 origin, ImVec2 size, bool hovered)
    {
        const SkeletonData& skeleton = Workspace.Audition.Session.Skeleton();
        const std::vector<AnimationJointMarker> markers = ProjectAnimationJoints(
            Workspace.Viewport.Model(), Viewport->ViewCamera(size.x / size.y).ViewProjection, size.x, size.y);
        std::vector<const AnimationJointMarker*> byJoint(skeleton.Joints.size(), nullptr);
        for (const AnimationJointMarker& marker : markers)
            if (marker.Joint < byJoint.size())
                byJoint[marker.Joint] = &marker;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        const ImU32 bone = IM_COL32(120, 200, 255, 160);
        const ImU32 joint = IM_COL32(120, 200, 255, 255);
        const ImU32 picked = IM_COL32(255, 170, 60, 255);
        for (const AnimationJointMarker& marker : markers)
        {
            const std::int32_t parent = skeleton.Joints[marker.Joint].ParentIndex;
            if (parent >= 0 && byJoint[static_cast<std::size_t>(parent)] != nullptr)
            {
                const AnimationJointMarker& from = *byJoint[static_cast<std::size_t>(parent)];
                draw->AddLine(ImVec2(origin.x + from.X, origin.y + from.Y),
                              ImVec2(origin.x + marker.X, origin.y + marker.Y), bone, 1.5f);
            }
        }
        for (const AnimationJointMarker& marker : markers)
        {
            const bool selected = Workspace.Navigation.Joint == static_cast<int>(marker.Joint);
            draw->AddCircleFilled(ImVec2(origin.x + marker.X, origin.y + marker.Y), selected ? 5.0f : 3.0f,
                                  selected ? picked : joint);
            if (selected)
                draw->AddText(ImVec2(origin.x + marker.X + 7.0f, origin.y + marker.Y - 7.0f), picked,
                              skeleton.Joints[marker.Joint].Name.c_str());
        }
        draw->PopClipRect();

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto pick = [&] {
            return PickAnimationJoint(markers, mouse.x - origin.x, mouse.y - origin.y, 8.0f);
        };
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            && ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f)
        {
            const std::optional<std::uint32_t> hit = pick();
            Workspace.Navigation.Joint = hit ? static_cast<int>(*hit) : -1;
        }
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            if (const std::optional<std::uint32_t> hit = pick())
            {
                Workspace.Navigation.Joint = static_cast<int>(*hit);
                ImGui::OpenPopup("joint");
            }
        if (ImGui::BeginPopup("joint"))
        {
            const int selected = Workspace.Navigation.Joint;
            if (selected >= 0 && static_cast<std::size_t>(selected) < skeleton.Joints.size())
                DrawAnimationMaskMenu(Workspace, skeleton.Joints[static_cast<std::size_t>(selected)].Name);
            ImGui::EndPopup();
        }
    }

public:
private:
    // Setup may refuse the staged feature. The host clears this slot before
    // any panel draws, while the panel remains available to explain failure.
    AnimationPreviewRenderFeature*& Viewport;
    AnimationPreviewWorkspace& Workspace;
    bool ShowJoints = false;
    bool ShowPath = true;
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
        ScopedPanel panel(GetTitle(), &Visible);
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
        ScopedPanel panel(GetTitle(), &Visible);
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

void AddAnimationPreviewPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace,
                               AnimationPreviewRenderFeature*& viewport)
{
    ui.AddPanel(std::make_unique<PreviewAssetsPanel>(workspace));
    ui.AddPanel(std::make_unique<PreviewViewportPanel>(viewport, workspace));
    ui.AddPanel(std::make_unique<PreviewTransportPanel>(workspace.Audition.Session));
    ui.AddPanel(std::make_unique<PreviewDetailsPanel>(workspace));
    AddAnimationSimulationPanels(ui, workspace);
    AddAnimationSelectionPanels(ui, workspace);
    AddAnimationEventPanels(ui, workspace);
    AddAnimationLayerPanels(ui, workspace);
    AddAnimationBlendPanels(ui, workspace);
    AddAnimationLabPanels(ui, workspace);
    AddAnimationRootMotionPanels(ui, workspace);
    AddAnimationDocumentPanels(ui, workspace);
    AddAnimationMigrationPanels(ui, workspace);
    AddAnimationScenarioBatchPanels(ui, workspace);
    auto requestSchema = std::make_unique<AnimationRequestSchemaPanel>(workspace);
    auto* requestSchemaPanel = requestSchema.get();
    ui.AddPanel(std::move(requestSchema));
    // Hidden panels do not receive OnDraw, including when hidden via View.
    ui.AddOverlay([&workspace, requestSchemaPanel] {
        if (!requestSchemaPanel->IsVisible()) workspace.Sources.CancelEdits();
    });
}
