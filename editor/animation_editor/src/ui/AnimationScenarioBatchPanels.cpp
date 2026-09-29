#include "ui/AnimationScenarioBatchPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <memory>
#include <string>

namespace
{
const char* VerdictText(AnimationScenarioVerdict verdict)
{
    switch (verdict)
    {
    case AnimationScenarioVerdict::Passed: return "passed";
    case AnimationScenarioVerdict::Warned: return "warning";
    case AnimationScenarioVerdict::Failed: return "failed";
    }
    return "";
}

ImVec4 VerdictColor(AnimationScenarioVerdict verdict)
{
    switch (verdict)
    {
    case AnimationScenarioVerdict::Passed: return ImVec4(0.5f, 0.85f, 0.55f, 1.0f);
    case AnimationScenarioVerdict::Warned: return ImVec4(0.95f, 0.8f, 0.4f, 1.0f);
    case AnimationScenarioVerdict::Failed: return ImVec4(1.0f, 0.5f, 0.4f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

class ScenarioBatchPanel final : public IEditorPanel
{
public:
    explicit ScenarioBatchPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Scenario batch"; }
    PanelPersistence GetPersistence() const override { return { "animation.scenario_batch" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        ImGui::TextWrapped("Runs every saved scenario in the project twice from the start, in a session of its own, "
                           "to two seconds past its last action. A scenario fails on an error or a second run that "
                           "differs from the first, and warns on a warning or a request nothing played.");
        if (ImGui::Button("Run all"))
            Runs = Workspace.RunScenarioBatch(false);
        ImGui::SameLine();
        ImGui::BeginDisabled(Workspace.Rig.Path.empty());
        if (ImGui::Button("Run all against the open rig"))
            Runs = Workspace.RunScenarioBatch(true);
        ImGui::EndDisabled();

        if (Runs.empty())
        {
            ImGui::TextDisabled("No results yet.");
            return;
        }
        if (!ImGui::BeginTable("runs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
            return;
        ImGui::TableSetupColumn("Verdict", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Scenario");
        ImGui::TableSetupColumn("Rig");
        ImGui::TableSetupColumn("Ending");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const AnimationScenarioRun& run : Runs)
        {
            ImGui::PushID(run.File.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(VerdictColor(run.Verdict), "%s", VerdictText(run.Verdict));
            ImGui::TableNextColumn();
            const bool open = ImGui::TreeNodeEx("run", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", run.File.c_str());
            ImGui::TableNextColumn();
            if (!run.RigPath.empty() && ImGui::SmallButton(run.RigPath.c_str()))
                (void)Workspace.OpenRig(run.RigPath);
            ImGui::TableNextColumn();
            for (const std::string& layer : run.Ending)
                ImGui::TextUnformatted(layer.c_str());
            if (open)
            {
                if (run.Ran && !run.Reproduces)
                    ImGui::BulletText("A second run from tick 0 did not match the first.");
                if (run.UnplayedRequests > 0)
                    ImGui::BulletText("%u requests ended without any layer playing them.", run.UnplayedRequests);
                for (const AnimDiagnostic& problem : run.Problems)
                    ImGui::BulletText("%s", FormatAnimDiagnostic(problem).c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

private:
    AnimationPreviewWorkspace& Workspace;
    std::vector<AnimationScenarioRun> Runs;
};
}

void AddAnimationScenarioBatchPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<ScenarioBatchPanel>(workspace));
}
