#include "ui/AnimationSimulationPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigDocumentEdits.h"
#include "ui/DocumentSaveReportView.h"
#include "authoring/AnimationTraceImport.h"
#include "ui/WorkspaceView.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimFactEvaluation.h>
#include <anim/AnimRequestReport.h>
#include <anim/AnimRigRisk.h>
#include <anim/AnimWorldReport.h>
#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

std::string TagName(const AnimationPreviewSession& session, std::uint32_t bits)
{
    const GameplayTagRegistry* tags = session.Tags();
    if (bits == 0 || tags == nullptr)
        return "(none)";
    const std::string_view name = tags->GetName(GameplayTagId{ bits });
    return name.empty() ? std::format("#{}", bits) : std::string(name);
}

std::string IntentName(const AnimationPreviewSession& session, GameplayTagId intent)
{
    if (const AnimBoundRig* rig = session.Rig())
    {
        if (const AnimBoundIntent* bound = rig->FindIntent(intent))
            return bound->Name;
    }
    return TagName(session, intent.Value);
}

class ProblemsChangesPanel final : public IEditorPanel
{
public:
    explicit ProblemsChangesPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Problems and changes"; }
    PanelPersistence GetPersistence() const override { return { "animation.problems" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetWindowName(), &Visible);
        if (!panel.IsOpen()) return;
        if (!ImGui::BeginTabBar("tabs")) return;
        if (ImGui::BeginTabItem("Problems"))
        {
            std::vector<AnimDiagnostic> problems = Workspace.Rig.Simulation.Problems();
            problems.insert(problems.end(), Workspace.Rig.LoadProblems.begin(),
                            Workspace.Rig.LoadProblems.end());
            if (problems.empty()) ImGui::TextDisabled("No problems with the open rig or scenario.");
            DrawDeclareNames(problems);
            for (const AnimDiagnostic& problem : problems)
                ImGui::TextWrapped("%s %s", problem.Severity == AnimDiagnosticSeverity::Error ? "Error" : "Warning",
                                   FormatAnimDiagnostic(problem).c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Decision history"))
        {
            DrawDecisions();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Content risk"))
        {
            DrawRisk();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Imported trace"))
        {
            DrawTrace();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Changes"))
        {
            DrawChanges();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
private:
    // Reads documents, so recomputed only when the problems change.
    void DrawDeclareNames(const std::vector<AnimDiagnostic>& problems)
    {
        std::string signature;
        for (const AnimDiagnostic& problem : problems)
            signature += problem.AssetPath + problem.FieldPath + problem.Code + "\n";
        if (signature != ProblemSignature)
        {
            ProblemSignature = std::move(signature);
            Undeclared = UndeclaredAnimationNamesOf(Workspace.Rig.Simulation, Workspace.Documents);
        }
        if (Undeclared.empty())
            return;
        std::string names;
        for (const std::string& name : Undeclared)
            names += (names.empty() ? "" : ", ") + name;
        ImGui::TextWrapped("Not declared anywhere: %s.", names.c_str());
        if (ImGui::Button("Declare them beside the rig"))
        {
            DeclareError.clear();
            (void)DeclareUndeclaredAnimationNames(Workspace.Documents, Workspace.Rig.Simulation, Workspace.Rig.Path, DeclareError);
            ProblemSignature.clear();
        }
        if (!DeclareError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", DeclareError.c_str());
        ImGui::Separator();
    }

    // Same measure `anim.risk` reports in a game.
    void DrawRisk()
    {
        const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
        if (rig == nullptr)
        {
            ImGui::TextDisabled("Open a rig to measure it.");
            return;
        }
        const AnimRigRisk risk = MeasureAnimRigRisk(*rig, Workspace.Rig.Simulation.Tags());
        ImGui::TextWrapped("%u blend overrides, %u selector rules (the largest selector weighs %u), %u flows "
                           "longer than %u sections.",
                           risk.BlendOverrides, risk.SelectorRules, risk.DeepestSelector, risk.LongFlows,
                           kAnimRiskFlowSections);
        ImGui::TextDisabled("Each count is a sign a rig is growing toward a graph; none of them is an error.");
        if (risk.Findings.empty())
            ImGui::TextDisabled("Nothing to look at.");
        for (const AnimRigRiskFinding& finding : risk.Findings)
            ImGui::BulletText("%s", finding.Message.c_str());

        const World* world = Workspace.Rig.Simulation.SimulationWorld();
        const EntityId subject = Workspace.Rig.Simulation.Subject();
        if (world == nullptr || !subject.IsValid())
            return;
        ImGui::SeparatorText("The previewed entity");
        ImGui::Text("%u bytes of animation state.", AnimEntityBytes(*world, subject));
        if (const AnimRequestReport* report = world->TryGet<AnimRequestReport>(subject))
            ImGui::Text("%u requests ended without any layer playing them.", report->Unplayed);
    }

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

    void DrawDecisions()
    {
        const AnimationPreviewSession& session = Workspace.Rig.Simulation;
        const AnimDecisionLog* log = session.DecisionLog();
        if (log == nullptr || log->Size() == 0)
        {
            ImGui::TextDisabled("Nothing has changed on the previewed entity yet.");
            return;
        }
        for (std::size_t i = log->Size(); i-- > 0;)
        {
            const AnimDecisionRecord& record = log->At(i);
            std::string text = std::format("{} {} #{} {}", record.Tick, AnimDecisionCauseName(record.Cause),
                                           record.Request.Sequence, IntentName(session, record.Intent));
            if (record.CancelReason != AnimCancelReason::None)
                text += std::format(" ({})", AnimCancelReasonName(record.CancelReason));
            if (record.RejectReason != AnimRejectReason::None)
                text += std::format(" ({})", AnimRejectReasonName(record.RejectReason));
            ImGui::TextUnformatted(text.c_str());
        }
    }

    void DrawChanges()
    {
        const AnimationPreviewSession& session = Workspace.Rig.Simulation;
        if (session.IsOpen())
            ImGui::TextWrapped("Scenario %s: %s", session.Scenario().Name.c_str(),
                               session.ScenarioModified() ? "recorded edits not saved" : "saved");
        DrawUnsavedDocuments(Workspace.Sources);
        if (ImGui::Button("Save all"))
            (void)Workspace.Sources.SaveAll();
        ImGui::TextWrapped("Preview inputs, requests and transport never modify a document.");
    }

    AnimationPreviewWorkspace& Workspace;
    std::array<char, 512> TracePath{};
    std::array<char, 128> TraceFilter{};
    std::optional<AnimationTrace> Trace;
    std::string TraceError;
    std::string ProblemSignature;
    std::vector<std::string> Undeclared;
    std::string DeclareError;
};
}

void AddAnimationSimulationPanels(WorkspaceView& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<ProblemsChangesPanel>(workspace));
}
