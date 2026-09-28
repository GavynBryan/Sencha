#include "ui/AnimationBlendPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimBlendspace.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimPoseEvaluation.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace
{
std::string TagText(const AnimationPreviewSession& session, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = session.Tags();
    return tag.IsValid() && tags != nullptr ? std::string(tags->GetName(tag)) : std::string("(none)");
}

std::string FileOf(const std::string& path)
{
    const std::size_t cut = path.find_last_of("/#");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

class BlendPanel final : public IEditorPanel
{
public:
    explicit BlendPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Blends"; }
    PanelPersistence GetPersistence() const override { return { "animation.blends" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        const AnimationPreviewSession& session = Workspace.Rig.Simulation;
        const AnimBoundRig* rig = session.Rig();
        if (rig == nullptr)
        {
            ImGui::TextDisabled("Open a rig to see how its changes blend.");
            return;
        }
        DrawLayers(session, *rig);
        DrawRecent(session, *rig);
        DrawOverrides(*rig);
        DrawComparison(*rig);
    }

private:
    void DrawLayers(const AnimationPreviewSession& session, const AnimBoundRig& rig)
    {
        const AnimPoseState* state = session.SubjectPoseState();
        const AnimPosePool::Slot* slot = session.SubjectPose();
        if (state == nullptr || slot == nullptr)
        {
            ImGui::TextDisabled(rig.SkeletonPath.empty() ? "The rig names no skeleton, so nothing poses it."
                                                         : "Nothing posed yet.");
            return;
        }
        const double dt = session.TickSeconds();
        const AnimTick now = slot->Tick;
        for (std::size_t l = 0; l < rig.Layers.size() && l < slot->Layers; ++l)
        {
            const AnimLayerPose& layer = state->Layers[l];
            ImGui::Text("%s: %s", rig.Layers[l].NameText.c_str(), TagText(session, layer.Playing.Behavior).c_str());
            ImGui::Indent();
            if (layer.Fading)
            {
                const float elapsed = static_cast<float>((now - layer.FadeStartTick) * dt);
                const float total = std::max(layer.FadeInSeconds, layer.FadeOutSeconds);
                ImGui::ProgressBar(total > 0.0f ? elapsed / total : 1.0f, ImVec2(-1.0f, 0.0f),
                                   std::format("crossfading from {}  {:.0f} / {:.0f} ms",
                                               TagText(session, layer.FadingOut.Behavior), elapsed * 1000.0f,
                                               total * 1000.0f)
                                       .c_str());
            }
            if (layer.Offsetting)
            {
                const float elapsed = static_cast<float>((now - layer.OffsetStartTick) * dt);
                const float remaining = AnimOffsetRemaining(
                    std::span<const AnimJointOffset>(slot->Offsets).subspan(l * slot->Joints, slot->Joints), elapsed);
                ImGui::ProgressBar(layer.OffsetSeconds > 0.0f ? elapsed / layer.OffsetSeconds : 1.0f,
                                   ImVec2(-1.0f, 0.0f),
                                   std::format("inertializing  {:.0f} / {:.0f} ms, {:.3f} left", elapsed * 1000.0f,
                                               layer.OffsetSeconds * 1000.0f, remaining)
                                       .c_str());
            }
            if (!layer.Fading && !layer.Offsetting)
                ImGui::TextDisabled("settled");
            ImGui::Unindent();
        }
    }

    void DrawRecent(const AnimationPreviewSession& session, const AnimBoundRig&)
    {
        ImGui::SeparatorText("Recent blends");
        std::vector<const AnimDecisionRecord*> recent;
        const auto& history = session.History();
        for (auto tick = history.rbegin(); tick != history.rend() && recent.size() < 12; ++tick)
            for (auto record = tick->Decisions.rbegin(); record != tick->Decisions.rend() && recent.size() < 12;
                 ++record)
                if (record->Cause == AnimDecisionCause::BlendApplied)
                    recent.push_back(&*record);
        if (recent.empty())
            ImGui::TextDisabled("No change has been blended yet.");
        for (const AnimDecisionRecord* record : recent)
        {
            ImGui::BulletText("tick %llu, layer %u: %s -> %s, %s %.0f ms%s%s",
                              static_cast<unsigned long long>(record->Tick), record->Layer,
                              TagText(session, record->PreviousBehavior).c_str(),
                              TagText(session, record->Behavior).c_str(),
                              std::string(AnimBlendModeName(record->Blend)).c_str(), record->BlendSeconds * 1000.0f,
                              record->Blend == AnimBlendMode::Inertialize
                                  ? std::format(", from {:.3f} away", record->BlendMagnitude).c_str()
                                  : "",
                              record->BlendOverridden ? " (pairwise override)" : "");
        }
    }

    void DrawOverrides(const AnimBoundRig& rig)
    {
        ImGui::SeparatorText("Pairwise overrides");
        ImGui::Text("%zu bound. Past half the cap a rig warns, past the cap it fails to bind "
                    "(anim.blend.override_cap).",
                    rig.BlendOverrides.size());
        const AnimationPreviewSession& session = Workspace.Rig.Simulation;
        for (const AnimBoundBlendOverride& entry : rig.BlendOverrides)
            ImGui::BulletText("%s -> %s: %s %.0f ms  (%s)", TagText(session, entry.From).c_str(),
                              TagText(session, entry.To).c_str(),
                              std::string(AnimBlendModeName(entry.Policy.In)).c_str(), entry.Policy.InMs,
                              entry.DeclaredIn.c_str());
    }

    void DrawComparison(const AnimBoundRig& rig)
    {
        ImGui::SeparatorText("A/B");
        ImGui::TextWrapped("Record A, edit a blend, then replay: B runs the same scenario from tick 0 -- same "
                           "inputs, clock, seed and start pose -- and is compared with A tick by tick. A is "
                           "drawn as an orange ghost.");
        if (ImGui::Button("Record A"))
            (void)Workspace.Takes.RecordA(Workspace.Rig.Simulation);
        ImGui::SameLine();
        ImGui::BeginDisabled(!Workspace.Takes.A());
        if (ImGui::Button("Replay B against A"))
            (void)Workspace.Takes.ReplayAgainstA(Workspace.Rig.Simulation);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            Workspace.Takes.Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Ghost", &Workspace.Viewport.ShowGhost);
        ImGui::EndDisabled();
        if (!Workspace.Takes.A())
            return;
        ImGui::Text("A: ticks %llu to %llu", static_cast<unsigned long long>(Workspace.Takes.A()->Ticks.front()),
                    static_cast<unsigned long long>(Workspace.Takes.A()->Ticks.back()));
        const AnimationPoseComparison& comparison = Workspace.Takes.Comparison();
        if (!comparison.Refusal.empty())
        {
            ImGui::TextWrapped("%s", comparison.Refusal.c_str());
            return;
        }
        if (comparison.Residuals.empty())
            return;
        std::vector<float> positions;
        const AnimationPoseResidual* worst = &comparison.Residuals.front();
        for (const AnimationPoseResidual& residual : comparison.Residuals)
        {
            positions.push_back(residual.Position);
            if (residual.Position > worst->Position)
                worst = &residual;
        }
        ImGui::PlotLines("Largest joint residual", positions.data(), static_cast<int>(positions.size()), 0, nullptr,
                         0.0f, FLT_MAX, ImVec2(-1.0f, 80.0f));
        const SkeletonData* skeleton = Workspace.Rig.Skeleton();
        const auto jointName = [&](std::uint32_t joint) {
            return skeleton != nullptr && joint < skeleton->Joints.size() && !skeleton->Joints[joint].Name.empty()
                ? skeleton->Joints[joint].Name
                : std::format("joint {}", joint);
        };
        if (worst->Position <= 0.0f && worst->Rotation <= 0.0f)
            ImGui::Text("B matches A on every tick.");
        else
            ImGui::Text("Largest at tick %llu: %.3f at %s, %.1f deg at %s",
                        static_cast<unsigned long long>(worst->Tick), worst->Position,
                        jointName(worst->PositionJoint).c_str(), worst->Rotation * 57.29578f,
                        jointName(worst->RotationJoint).c_str());
        (void)rig;
    }

    AnimationPreviewWorkspace& Workspace;
};

class BlendspacePanel final : public IEditorPanel
{
public:
    explicit BlendspacePanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Blendspace"; }
    PanelPersistence GetPersistence() const override { return { "animation.blendspace" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Workspace.Rig.Simulation;
        const AnimBoundRig* rig = session.Rig();
        const AnimContentState* content = session.Content();
        const std::size_t l = Workspace.Rig.Navigation.Layer;
        if (rig == nullptr || content == nullptr || l >= rig->Layers.size())
        {
            ImGui::TextDisabled("Run a rig to see the blendspace its selected layer plays.");
            return;
        }
        const AnimLayerContent& layer = content->Layers[l];
        if (layer.Content >= rig->Contents.size() || rig->Contents[layer.Content].Blendspace < 0)
        {
            ImGui::TextDisabled("%s is not playing a blendspace.", rig->Layers[l].NameText.c_str());
            return;
        }
        const AnimBoundBlendspace& space =
            rig->Blendspaces[static_cast<std::size_t>(rig->Contents[layer.Content].Blendspace)];
        ImGui::Text("%s on %s", space.Path.c_str(), rig->Layers[l].NameText.c_str());

        std::array<float, kAnimBlendspaceMaxSamples> weights{};
        AnimBlendspaceWeights(space, { layer.Coordinates[0], layer.Coordinates[1] }, weights);
        const bool twoAxes = space.AxisCount > 1;

        // Axis ranges mapped onto the canvas: a plane for two axes, a line for one.
        const float width = std::max(ImGui::GetContentRegionAvail().x, 160.0f);
        const float height = twoAxes ? std::min(width, 260.0f) : 48.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("space", ImVec2(width, height));
        const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg));
        const AnimBoundBlendspaceAxis& ax = space.Axes[0];
        const AnimBoundBlendspaceAxis& ay = space.Axes[1];
        const auto toCanvas = [&](float x, float y) {
            const float u = (x - ax.Min) / (ax.Max - ax.Min);
            const float v = twoAxes ? (y - ay.Min) / (ay.Max - ay.Min) : 0.5f;
            return ImVec2(origin.x + 8.0f + u * (width - 16.0f), origin.y + height - 8.0f - v * (height - 16.0f));
        };
        for (std::size_t s = 0; s < space.Samples.size(); ++s)
        {
            const ImVec2 at = toCanvas(space.Samples[s].At[0], space.Samples[s].At[1]);
            const float radius = 3.0f + 8.0f * weights[s];
            draw->AddCircleFilled(at, radius, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
            const int sampleContent = space.Samples[s].Content;
            if (sampleContent >= 0)
                draw->AddText(ImVec2(at.x + 6.0f, at.y - 16.0f), ImGui::GetColorU32(ImGuiCol_Text),
                              FileOf(rig->Contents[static_cast<std::size_t>(sampleContent)].Path).c_str());
        }
        const ImVec2 point = toCanvas(layer.Coordinates[0], layer.Coordinates[1]);
        draw->AddCircle(point, 7.0f, ImGui::GetColorU32(ImGuiCol_Text), 0, 2.0f);

        // A live fact edit the scenario records, never an asset change.
        if (dragging)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float u = std::clamp((mouse.x - origin.x - 8.0f) / (width - 16.0f), 0.0f, 1.0f);
            const float v = std::clamp((origin.y + height - 8.0f - mouse.y) / (height - 16.0f), 0.0f, 1.0f);
            const std::array<float, 2> value{ ax.Min + u * (ax.Max - ax.Min), ay.Min + v * (ay.Max - ay.Min) };
            for (std::size_t a = 0; a < space.AxisCount; ++a)
            {
                AnimationScenarioValue set;
                set.Number = value[a];
                session.SetFact(space.Axes[a].Fact, set);
            }
        }
        ImGui::TextDisabled("Drag to set %s%s%s for the next tick.", ax.Fact.c_str(), twoAxes ? " and " : "",
                            twoAxes ? ay.Fact.c_str() : "");

        ImGui::Text("At %s = %.2f%s", ax.Fact.c_str(), layer.Coordinates[0],
                    twoAxes ? std::format(", {} = {:.2f}", ay.Fact, layer.Coordinates[1]).c_str() : "");
        ImGui::ProgressBar(layer.Phase, ImVec2(-1.0f, 0.0f), std::format("phase {:.2f}", layer.Phase).c_str());
        if (ImGui::BeginTable("weights", 3, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Sample");
            ImGui::TableSetupColumn("At");
            ImGui::TableSetupColumn("Weight");
            ImGui::TableHeadersRow();
            for (std::size_t s = 0; s < space.Samples.size(); ++s)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const int sampleContent = space.Samples[s].Content;
                ImGui::TextUnformatted(sampleContent >= 0
                                           ? FileOf(rig->Contents[static_cast<std::size_t>(sampleContent)].Path).c_str()
                                           : "?");
                ImGui::TableNextColumn();
                ImGui::Text(twoAxes ? "%.2f, %.2f" : "%.2f", space.Samples[s].At[0], space.Samples[s].At[1]);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f%s", weights[s],
                            layer.Clip == static_cast<std::uint16_t>(sampleContent) ? "  events" : "");
            }
            ImGui::EndTable();
        }
    }

private:
    AnimationPreviewWorkspace& Workspace;
};
}

void AddAnimationBlendPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<BlendPanel>(workspace));
    ui.AddPanel(std::make_unique<BlendspacePanel>(workspace));
}
