#include "AnimationPreviewPanels.h"

#include "AnimationPreviewWorkspace.h"
#include "ui/AnimationRequestSchemaPanel.h"
#include "render/AnimationPreviewRenderFeature.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <memory>
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
        ImGui::TextWrapped("Preview selections are transient. Request schemas open as editable documents.");
        if (ImGui::Button("Refresh asset list")) Workspace.RefreshBrowser();
        if (ImGui::CollapsingHeader("Request schemas", ImGuiTreeNodeFlags_DefaultOpen))
            for (const auto& path : Workspace.RequestSchemaPaths)
                if (ImGui::Selectable(path.c_str())) Workspace.OpenRequestSchema(path);
        DrawAssets("Skinned meshes", Workspace.MeshPaths, Workspace.MeshPath,
                   &AnimationPreviewWorkspace::SelectMesh);
        DrawAssets("Skeletons (without mesh)", Workspace.SkeletonPaths, Workspace.Session.SkeletonPath(),
                   &AnimationPreviewWorkspace::SelectSkeleton);
        if (ImGui::Button("Bind pose")) Workspace.SelectClip({});
        DrawAssets("Clips", Workspace.ClipPaths, Workspace.ClipPath,
                   &AnimationPreviewWorkspace::SelectClip);
        if (ImGui::Button("Neutral preview material")) Workspace.SelectMaterial({});
        DrawAssets("Material override", Workspace.MaterialPaths, Workspace.MaterialPath,
                   &AnimationPreviewWorkspace::SelectMaterial);
    }
private:
    void DrawAssets(const char* title, const std::vector<std::string>& paths,
                    const std::string& selected,
                    bool (AnimationPreviewWorkspace::*select)(const std::string&))
    {
        if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::PushID(title);
        for (const auto& path : paths)
        {
            if (ImGui::Selectable(path.c_str(), path == selected)) (Workspace.*select)(path);
        }
        if (paths.empty()) ImGui::TextDisabled("No assets of this kind in the mounted project.");
        ImGui::PopID();
    }
    AnimationPreviewWorkspace& Workspace;
};

class PreviewViewportPanel final : public IEditorPanel
{
public:
    explicit PreviewViewportPanel(AnimationPreviewRenderFeature*& viewport) : Viewport(viewport) {}
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
        ImGui::TextDisabled("Drag to orbit; wheel to zoom");
        const auto size = ImGui::GetContentRegionAvail();
        if (size.x < 8.0f || size.y < 8.0f) return;
        const auto texture = Viewport->Display({ static_cast<std::uint32_t>(size.x),
                                                static_cast<std::uint32_t>(size.y) });
        if (!texture) return;
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::Image(texture, size);
        ImGui::SetCursorScreenPos(position);
        ImGui::InvisibleButton("orbit", size, ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemHovered()) Viewport->Zoom(ImGui::GetIO().MouseWheel);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const auto delta = ImGui::GetIO().MouseDelta;
            Viewport->Orbit(delta.x * 0.01f, -delta.y * 0.01f);
        }
    }
private:
    // Setup may refuse the staged feature. The host clears this slot before
    // any panel draws, while the panel remains available to explain failure.
    AnimationPreviewRenderFeature*& Viewport;
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
        if (!Workspace.Error.empty())
        {
            ImGui::TextWrapped("Selection rejected: %s", Workspace.Error.c_str());
            ImGui::TextWrapped("The previous valid content remains selected.");
        }
        ImGui::TextWrapped("Mesh: %s", Workspace.MeshPath.c_str());
        ImGui::TextWrapped("Skeleton: %s", Workspace.Session.SkeletonPath().c_str());
        ImGui::TextWrapped("Clip: %s", Workspace.ClipPath.empty() ? "Bind pose" : Workspace.ClipPath.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("This surface auditions cooked clips. Selectors, requests, event tracks and gameplay simulation are not enabled here yet.");
        const auto& joints = Workspace.Session.Skeleton().Joints;
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
    ui.AddPanel(std::make_unique<PreviewViewportPanel>(viewport));
    ui.AddPanel(std::make_unique<PreviewTransportPanel>(workspace.Session));
    ui.AddPanel(std::make_unique<PreviewDetailsPanel>(workspace));
    auto requestSchema = std::make_unique<AnimationRequestSchemaPanel>(workspace);
    auto* requestSchemaPanel = requestSchema.get();
    ui.AddPanel(std::move(requestSchema));
    // Hidden panels do not receive OnDraw, including when hidden via View.
    ui.AddOverlay([&workspace, requestSchemaPanel] {
        if (!requestSchemaPanel->IsVisible()) workspace.CancelAuthoringEdit();
    });
}
