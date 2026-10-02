#include "ui/AnimationSimulationPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationTraceImport.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"


#include <imgui.h>

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace
{
// Until the trace bench replaces it.
class TracePanel final : public IEditorPanel
{
public:
    explicit TracePanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Imported trace"; }
    PanelPersistence GetPersistence() const override { return { "animation.trace" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;
        DrawTrace();
    }
private:
    void DrawTrace()
    {
        ImGui::SetNextItemWidth(-160.0f);
        ImGui::InputTextWithHint("##trace", "Trace file exported with anim.trace.export", TracePath.data(),
                                 TracePath.size());
        ImGui::SameLine();
        if (ImGui::Button("Open trace"))
        {
            TraceError.clear();
            Trace = ReadAnimationTraceFile(std::string(TracePath.data()), TraceError);
        }
        if (!TraceError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", TraceError.c_str());
        if (!Trace)
            return;

        ImGui::Text("Entity %s, rig %s", Trace->Entity.c_str(), Trace->Rig.empty() ? "(unbound)" : Trace->Rig.c_str());
        if (!Trace->Rig.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open its rig") && !Workspace.OpenRig(Trace->Rig))
                TraceError = Workspace.Rig.Error;
        }
        ImGui::TextWrapped("Captured: %s. There is no pose history to scrub; these are the game's decisions as "
                           "it logged them.",
                           Trace->Captured.empty() ? "unknown" : Trace->Captured.c_str());
        if (Trace->Overwritten() > 0)
            ImGui::TextWrapped("The %llu earliest records were overwritten before export.",
                               static_cast<unsigned long long>(Trace->Overwritten()));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter by cause, layer or detail", TraceFilter.data(),
                                 TraceFilter.size());
        const std::string_view filter(TraceFilter.data());
        if (!ImGui::BeginTable("records", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                                                 | ImGuiTableFlags_ScrollY))
            return;
        ImGui::TableSetupColumn("Tick", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cause", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Detail");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const AnimationTraceRow& row : Trace->Rows)
        {
            if (!filter.empty() && row.Cause.find(filter) == std::string::npos
                && row.Layer.find(filter) == std::string::npos && row.Detail.find(filter) == std::string::npos)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.Tick));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Cause.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Layer.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", row.Detail.c_str());
        }
        ImGui::EndTable();
    }

    AnimationPreviewWorkspace& Workspace;
    std::array<char, 512> TracePath{};
    std::array<char, 128> TraceFilter{};
    std::optional<AnimationTrace> Trace;
    std::string TraceError;
};
}

void AddAnimationSimulationPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<TracePanel>(workspace));
}
