#include "ui/AnimationLabPanels.h"

#include "authoring/AnimationPreviewWorkspace.h"
#include "ui/EditorUiFeature.h"
#include "ui/IEditorPanel.h"
#include "ui/ScopedPanel.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <imgui.h>

#include <algorithm>
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

std::string AnchorText(const AnimRequest& request)
{
    if (request.AnchorSection == kAnimNoAnchorSection)
        return "-";
    return std::format("section {} from {}", request.AnchorSection, request.AnchorSectionStartTick);
}

const AnimationPreviewTickRecord* RecordAt(const AnimationPreviewSession& session, AnimTick tick)
{
    const auto& history = session.History();
    if (history.empty() || tick < history.front().Tick || tick > history.back().Tick)
        return nullptr;
    return &history[static_cast<std::size_t>(tick - history.front().Tick)];
}

void DrawRequests(const char* id, const AnimationPreviewSession& session, const std::vector<AnimRequest>& requests)
{
    if (requests.empty())
    {
        ImGui::TextDisabled("No requests.");
        return;
    }
    if (!ImGui::BeginTable(id, 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Intent");
    ImGui::TableSetupColumn("Seq");
    ImGui::TableSetupColumn("Start");
    ImGui::TableSetupColumn("Anchor");
    ImGui::TableSetupColumn("Cancel");
    ImGui::TableSetupColumn("Guess");
    ImGui::TableHeadersRow();
    for (const AnimRequest& request : requests)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(TagText(session, request.Intent).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%u", request.Id.Sequence);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(request.StartTick));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(AnchorText(request).c_str());
        ImGui::TableNextColumn();
        if (request.IsCancelled())
            ImGui::Text("at %llu, tail to %llu", static_cast<unsigned long long>(request.CancelTick),
                        static_cast<unsigned long long>(request.TailUntilTick));
        else
            ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(request.Predicted ? "yes" : "");
    }
    ImGui::EndTable();
}

class LabPanel final : public IEditorPanel
{
public:
    explicit LabPanel(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}
    std::string_view GetTitle() const override { return "Session lab"; }
    PanelPersistence GetPersistence() const override { return { "animation.session_lab" }; }
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    void OnDraw() override
    {
        if (!IsVisible()) return;
        ScopedPanel panel(GetTitle(), &Visible);
        if (!panel.IsOpen()) return;

        const AnimationSessionLab* lab = Workspace.Lab.Session.get();
        const bool ran = lab != nullptr && lab->IsOpen();
        // Always shown first: which machine is which, and that facts are synthetic.
        if (ran)
            ImGui::TextWrapped("%s", lab->Status().c_str());
        else
            ImGui::TextWrapped("Not run. The lab plays the working scenario on an authority, and on a client that "
                               "receives the authority's requests over a link. Facts are synthetic scenario inputs "
                               "on both.");
        ImGui::Separator();

        if (!Workspace.Rig.Simulation.IsOpen())
        {
            ImGui::TextDisabled("Open a rig to run it on two machines.");
            return;
        }
        DrawControls(ran);
        DrawInjections();
        if (!ran)
            return;
        ImGui::Separator();
        DrawSummary(*lab);
        DrawTimeline(*lab);
        DrawInspected(*lab);
    }

private:
    void DrawControls(bool ran)
    {
        AnimationLabSettings& settings = Workspace.Lab.Settings;
        const AnimationLabSettings before = settings;
        const std::uint64_t step = 1;
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Join on tick", ImGuiDataType_U64, &settings.JoinTick, &step);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Latency (ticks)", ImGuiDataType_U32, &settings.LatencyTicks, &step);
        ImGui::SameLine();
        int loss = static_cast<int>(settings.LossPercent);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderInt("Loss %", &loss, 0, 90))
            settings.LossPercent = static_cast<std::uint32_t>(loss);
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Run to tick", ImGuiDataType_U64, &Workspace.Lab.Tick, &step);
        ImGui::SameLine();
        if (ImGui::Button(ran ? "Run again" : "Run"))
        {
            if (!Workspace.Lab.Run(Workspace.Rig.Simulation))
                Problem = "The rig did not open on both machines.";
            else
                Problem.clear();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runs both machines from tick 0 under the working scenario and the settings above.");
        if (ran && !(before == settings && Workspace.Lab.Session->Settings() == settings))
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(settings changed; run again)");
        }
        if (!Problem.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Problem.c_str());
    }

    void DrawInjections()
    {
        if (!ImGui::CollapsingHeader("Client guesses"))
            return;
        ImGui::TextDisabled("A request the client predicts, and what the authority does with the command behind it.");
        std::vector<AnimationLabInjection>& injections = Workspace.Lab.Injections;
        const std::vector<std::string>& participants = Workspace.Rig.Simulation.Scenario().Participants;
        std::size_t removed = injections.size();
        for (std::size_t i = 0; i < injections.size(); ++i)
        {
            AnimationLabInjection& injection = injections[i];
            ImGui::PushID(static_cast<int>(i));
            const std::uint64_t step = 1;
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputScalar("guessed on", ImGuiDataType_U64, &injection.Tick, &step);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::BeginCombo("by", injection.Participant.c_str()))
            {
                for (const std::string& name : participants)
                    if (ImGui::Selectable(name.c_str(), name == injection.Participant))
                        injection.Participant = name;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            char intent[128] = {};
            injection.Intent.copy(intent, sizeof(intent) - 1);
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::InputText("intent", intent, sizeof(intent)))
                injection.Intent = intent;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputScalar("decided on", ImGuiDataType_U64, &injection.AuthorityTick, &step);
            ImGui::SameLine();
            ImGui::Checkbox("confirmed", &injection.Confirmed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                removed = i;
            ImGui::PopID();
        }
        if (removed < injections.size())
            injections.erase(injections.begin() + static_cast<std::ptrdiff_t>(removed));
        if (ImGui::SmallButton("Add a guess"))
        {
            AnimationLabInjection injection;
            injection.Tick = Workspace.Rig.Simulation.Tick();
            injection.AuthorityTick = injection.Tick + 2;
            if (!participants.empty())
                injection.Participant = participants.front();
            injections.push_back(std::move(injection));
        }
    }

    void DrawSummary(const AnimationSessionLab& lab)
    {
        std::uint32_t lost = 0;
        std::uint32_t delivered = 0;
        for (const AnimationLabTick& tick : lab.Ticks())
        {
            lost += tick.Lost;
            delivered += tick.Delivered;
        }
        ImGui::Text("Snapshots delivered %u, lost %u.", delivered, lost);
        ImGui::SameLine();
        if (lab.ConvergedAt())
            ImGui::Text("Agreeing from tick %llu.", static_cast<unsigned long long>(*lab.ConvergedAt()));
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Not agreeing at the last tick.");
        if (lab.PendingPredictions() > 0)
        {
            ImGui::SameLine();
            ImGui::Text("%zu guesses waiting on the authority.", lab.PendingPredictions());
        }
        if (!lab.Ticks().empty() && lab.Ticks().back().TimingDisagrees)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "The client binds this rig with different timing from the authority's: "
                               "reconstruction cannot be trusted until both load the same content.");
    }

    void DrawTimeline(const AnimationSessionLab& lab)
    {
        const std::vector<AnimationLabTick>& ticks = lab.Ticks();
        if (ticks.empty())
            return;
        const float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
        const float height = 18.0f;
        const float cell = width / static_cast<float>(ticks.size());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("timeline", ImVec2(width, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (std::size_t i = 0; i < ticks.size(); ++i)
        {
            const AnimationLabTick& tick = ticks[i];
            const ImU32 colour = !tick.Joined ? IM_COL32(90, 90, 90, 255)
                               : tick.Agrees  ? IM_COL32(70, 170, 110, 255)
                                              : IM_COL32(210, 80, 70, 255);
            const float x = origin.x + cell * static_cast<float>(i);
            draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + std::max(cell, 1.0f), origin.y + height), colour);
            if (tick.Lost > 0)
                draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + std::max(cell, 1.0f), origin.y + 4.0f),
                                    IM_COL32(20, 20, 20, 255));
            if (tick.Tick == Inspected)
                draw->AddRect(ImVec2(x, origin.y), ImVec2(x + std::max(cell, 2.0f), origin.y + height),
                              IM_COL32(255, 255, 255, 255));
        }
        if (ImGui::IsItemHovered())
        {
            const float at = (ImGui::GetIO().MousePos.x - origin.x) / cell;
            const std::size_t index = std::min(ticks.size() - 1, static_cast<std::size_t>(std::max(at, 0.0f)));
            const AnimationLabTick& tick = ticks[index];
            ImGui::SetTooltip("tick %llu: %s\ndelivered %u, lost %u\npose residual %.4f m, %.4f rad",
                              static_cast<unsigned long long>(tick.Tick),
                              !tick.Joined ? "not joined" : (tick.Agrees ? "agrees" : "disagrees"), tick.Delivered,
                              tick.Lost, tick.Pose.Position, tick.Pose.Rotation);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                Inspected = tick.Tick;
        }

        std::vector<float> residual;
        residual.reserve(ticks.size());
        for (const AnimationLabTick& tick : ticks)
            residual.push_back(tick.Pose.Position);
        ImGui::PlotLines("##residual", residual.data(), static_cast<int>(residual.size()), 0,
                         "client pose minus authority's (m)", 0.0f, FLT_MAX, ImVec2(width, 50.0f));
    }

    void DrawInspected(const AnimationSessionLab& lab)
    {
        if (ImGui::CollapsingHeader("What the joiner received"))
        {
            if (lab.JoinedAt())
            {
                ImGui::Text("First snapshot on tick %llu.", static_cast<unsigned long long>(*lab.JoinedAt()));
                DrawRequests("joined", lab.Client(), lab.JoinRequests());
            }
            else
            {
                ImGui::TextDisabled("Nothing has arrived yet.");
            }
        }

        Inspected = std::min<AnimTick>(Inspected, lab.Tick());
        ImGui::Text("Tick %llu", static_cast<unsigned long long>(Inspected));
        const AnimationPreviewTickRecord* here = RecordAt(lab.Authority(), Inspected);
        const AnimationPreviewTickRecord* there = RecordAt(lab.Client(), Inspected);
        if (here == nullptr || there == nullptr)
        {
            ImGui::TextDisabled("That tick is no longer kept.");
            return;
        }
        if (ImGui::BeginTable("machines", 2, ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Authority");
            ImGui::TableSetupColumn("Client");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawMachine("authority", lab.Authority(), *here);
            ImGui::TableNextColumn();
            DrawMachine("client", lab.Client(), *there);
            ImGui::EndTable();
        }
    }

    static void DrawMachine(const char* id, const AnimationPreviewSession& session,
                            const AnimationPreviewTickRecord& record)
    {
        ImGui::PushID(id);
        for (std::size_t l = 0; l < record.Layers.size(); ++l)
        {
            const AnimationPreviewLayerRecord& layer = record.Layers[l];
            ImGui::Text("layer %zu: %s, %.3f s", l, TagText(session, layer.Behavior).c_str(), layer.TimeSeconds);
            if (layer.Flow.Phase != AnimFlowPhase::None)
                ImGui::TextDisabled("  section %u from tick %llu, loop %u", layer.Flow.Section,
                                    static_cast<unsigned long long>(layer.Flow.SectionStartTick),
                                    layer.Flow.LoopCount);
        }
        DrawRequests("requests", session, record.Requests);
        for (const AnimDecisionRecord& decision : record.Decisions)
            ImGui::TextDisabled("%s %s", std::string(AnimDecisionCauseName(decision.Cause)).c_str(),
                                std::string(AnimChangeReasonName(decision.Reason)).c_str());
        ImGui::PopID();
    }

    AnimationPreviewWorkspace& Workspace;
    AnimTick Inspected = 0;
    std::string Problem;
};
}

void AddAnimationLabPanels(EditorUiFeature& ui, AnimationPreviewWorkspace& workspace)
{
    ui.AddPanel(std::make_unique<LabPanel>(workspace));
}
