#include "ui/AnimationRootMotionPanels.h"

#include "authoring/AnimationRigScenario.h"
#include "authoring/AnimationViewportExtraction.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimRootMotion.h>
#include <anim/AnimationClipCache.h>

#include <imgui.h>

#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace
{
class RootMotionPanel final : public IEditorPanel
{
public:
    RootMotionPanel(AnimationRigScenario& rig, const AnimationClipCache& clips) : Rig(rig), Clips(clips) {}
    std::string_view GetTitle() const override { return "Root motion"; }
    PanelPersistence GetPersistence() const override { return { "animation.root_motion" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;

        AnimationPreviewSession& session = Rig.Simulation;
        if (!session.IsOpen() || session.Rig() == nullptr)
        {
            ImGui::TextDisabled("Open a rig to see what its clips carry.");
            return;
        }
        DrawMovement(session);
        ImGui::Separator();
        DrawCurves(session);
        ImGui::Separator();
        DrawTravel(session);
    }

private:
    void DrawMovement(AnimationPreviewSession& session)
    {
        std::optional<AnimationScenarioMovement> movement = session.Scenario().Movement;
        bool moves = movement.has_value();
        if (ImGui::Checkbox("Move the character", &moves))
        {
            session.SetMovement(moves ? std::optional(AnimationScenarioMovement{}) : std::nullopt);
            return;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stand the character on a floor and move it through the game's movement pipeline, "
                              "carried by root motion and stopped by walls. Off, it poses in place.");
        if (!movement)
            return;

        bool changed = false;
        std::size_t removed = movement->Walls.size();
        for (std::size_t i = 0; i < movement->Walls.size(); ++i)
        {
            AnimationScenarioWall& wall = movement->Walls[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputFloat3("centre", &wall.Center.X, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputFloat3("half size", &wall.HalfExtents.X, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                removed = i;
            ImGui::PopID();
        }
        if (removed < movement->Walls.size())
        {
            movement->Walls.erase(movement->Walls.begin() + static_cast<std::ptrdiff_t>(removed));
            changed = true;
        }
        if (ImGui::SmallButton("Add a wall ahead"))
        {
            // Two metres ahead of the start position, facing the character.
            movement->Walls.push_back(AnimationScenarioWall{ .Center = Vec3d(0.0f, 1.0f, -2.0f),
                                                             .HalfExtents = Vec3d(2.0f, 1.0f, 0.25f) });
            changed = true;
        }
        for (AnimationScenarioWall& wall : movement->Walls)
            wall.HalfExtents = Vec3d(std::max(wall.HalfExtents.X, 0.01f), std::max(wall.HalfExtents.Y, 0.01f),
                                     std::max(wall.HalfExtents.Z, 0.01f));
        if (changed)
            session.SetMovement(std::move(movement));
    }

    void DrawCurves(const AnimationPreviewSession& session)
    {
        const AnimBoundRig& rig = *session.Rig();
        const AnimationPreviewTickRecord* record = Shown(session);
        if (record == nullptr || record->Layers.empty())
        {
            ImGui::TextDisabled("Nothing has played yet.");
            return;
        }
        const AnimationPreviewLayerRecord& base = record->Layers.front();
        const AnimBoundBehavior* behavior = rig.FindBehavior(base.Behavior);
        const AnimationClipData* clip =
            base.Clip < rig.Contents.size() ? Clips.Get(rig.Contents[base.Clip].Clip) : nullptr;
        if (clip == nullptr)
        {
            ImGui::TextDisabled("The base layer plays no clip.");
            return;
        }
        const std::string name = rig.Contents[base.Clip].Path;
        if (!clip->Root.has_value())
        {
            ImGui::TextWrapped("%s carries no root motion. Ask for it with \"root_motion\": true on the clip in its "
                               "source's import sidecar.",
                               name.c_str());
            return;
        }
        ImGui::TextWrapped("%s%s", name.c_str(),
                           behavior != nullptr && behavior->Policy.RootMotion
                               ? ": carrying the character"
                               : " (its behavior is not root motion, so it plays in place)");
        constexpr int kSamples = 64;
        std::array<float, kSamples> x{};
        std::array<float, kSamples> z{};
        std::array<float, kSamples> yaw{};
        for (int i = 0; i < kSamples; ++i)
        {
            const AnimRootPose pose =
                SampleAnimRootCurve(*clip->Root, clip->DurationSeconds * static_cast<float>(i) / (kSamples - 1));
            x[i] = pose.X;
            z[i] = pose.Z;
            yaw[i] = pose.Yaw * 57.29578f;
        }
        const float width = std::max(ImGui::GetContentRegionAvail().x / 3.0f - 8.0f, 60.0f);
        const AnimRootPose now = SampleAnimRootCurve(*clip->Root, base.TimeSeconds);
        ImGui::PlotLines("##x", x.data(), kSamples, 0, std::format("right {:.2f} m", now.X).c_str(), FLT_MAX, FLT_MAX,
                         ImVec2(width, 60.0f));
        ImGui::SameLine();
        ImGui::PlotLines("##z", z.data(), kSamples, 0, std::format("back {:.2f} m", now.Z).c_str(), FLT_MAX, FLT_MAX,
                         ImVec2(width, 60.0f));
        ImGui::SameLine();
        ImGui::PlotLines("##yaw", yaw.data(), kSamples, 0, std::format("turn {:.0f} deg", now.Yaw * 57.29578f).c_str(),
                         FLT_MAX, FLT_MAX, ImVec2(width, 60.0f));
    }

    void DrawTravel(const AnimationPreviewSession& session)
    {
        if (!session.Scenario().Movement)
        {
            ImGui::TextDisabled("The character poses in place; move it to compare carried and achieved paths.");
            return;
        }
        const std::optional<AnimTick> shown = ShownAnimationTick(Rig.Simulation, Rig.Navigation);
        float requested = 0.0f;
        float achieved = 0.0f;
        std::vector<AnimTick> blocked;
        for (const AnimationPreviewTickRecord& record : session.History())
        {
            if (!record.Movement || (shown && record.Tick > *shown) || !record.Movement->Carried)
                continue;
            requested += record.Movement->Requested.Magnitude();
            achieved += record.Movement->Achieved.Magnitude();
            if (record.Movement->Blocked)
                blocked.push_back(record.Tick);
        }
        ImGui::Text("Carried %.2f m, achieved %.2f m.", requested, achieved);
        if (blocked.empty())
        {
            ImGui::TextDisabled("Nothing stopped it.");
            return;
        }
        std::string ticks;
        for (std::size_t i = 0; i < blocked.size() && i < 12; ++i)
            ticks += std::format("{}{}", i == 0 ? "" : ", ", blocked[i]);
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.3f, 1.0f), "Stopped short on %zu ticks: %s%s", blocked.size(),
                           ticks.c_str(), blocked.size() > 12 ? ", ..." : "");
    }

    const AnimationPreviewTickRecord* Shown(const AnimationPreviewSession& session) const
    {
        const std::optional<AnimTick> tick = ShownAnimationTick(Rig.Simulation, Rig.Navigation);
        for (const AnimationPreviewTickRecord& record : session.History())
            if (tick && record.Tick == *tick)
                return &record;
        return session.History().empty() ? nullptr : &session.History().back();
    }

    AnimationRigScenario& Rig;
    const AnimationClipCache& Clips;
};
}

void AddAnimationRootMotionPanels(EditorUiFeature& ui, AnimationRigScenario& rig, const AnimationClipCache& clips)
{
    ui.AddPanel(std::make_unique<RootMotionPanel>(rig, clips));
}
