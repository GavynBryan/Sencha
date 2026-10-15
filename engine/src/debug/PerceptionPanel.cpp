#include <debug/PerceptionPanel.h>

#include <ecs/World.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionSystem.h>

#include <imgui.h>

#include <cstdio>

namespace
{
    const char* OutcomeName(SightOutcome outcome)
    {
        switch (outcome)
        {
        case SightOutcome::Seen: return "seen";
        case SightOutcome::OutOfRange: return "out of range";
        case SightOutcome::OutsideCone: return "outside cone";
        case SightOutcome::Blocked: return "blocked";
        case SightOutcome::Untested: return "untested (ray cap)";
        }
        return "?";
    }

    const char* CoverageName(ScanCoverage coverage)
    {
        switch (coverage)
        {
        case ScanCoverage::NotEvaluated: return "not evaluated";
        case ScanCoverage::Partial: return "partial";
        case ScanCoverage::Complete: return "complete";
        }
        return "?";
    }
}

PerceptionPanel::PerceptionPanel(const World& world, PerceptionSystem& perception)
    : Entities(world)
    , Perception(perception)
{
}

void PerceptionPanel::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Perception"))
    {
        ImGui::End();
        return;
    }

    const SightStats& stats = Perception.LastSightStats();
    ImGui::Text("sensors %u (unusable %u)  targets %u  candidates %u", stats.Sensors, stats.UnusableSensors,
                stats.Targets, stats.Candidates);
    ImGui::Text("seen %u  blocked %u  out of range %u  outside cone %u  untested %u", stats.Seen,
                stats.Blocked, stats.OutOfRange, stats.OutsideCone, stats.Untested);
    ImGui::Text("rays %u  partial scans %u", stats.Raycasts, stats.PartialScans);

    const PerceptionFrame* frame = Entities.TryGetResource<PerceptionFrame>();
    const EntityId selected = Perception.SelectedObserver();
    ImGui::SeparatorText("Observers");
    for (const EntityId observer : Perception.LastObservers())
    {
        const ScanCoverage coverage =
            frame != nullptr ? frame->CoverageOf(observer) : ScanCoverage::NotEvaluated;
        char label[96];
        std::snprintf(label, sizeof(label), "entity %u:%u  %s", observer.Index, observer.Generation,
                      CoverageName(coverage));
        if (ImGui::Selectable(label, observer == selected))
            Perception.SelectObserver(observer == selected ? EntityId{} : observer);
    }

    if (!selected.IsValid())
    {
        ImGui::TextDisabled("select an observer to see each candidate");
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Candidates");
    if (ImGui::BeginTable("candidates", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("target");
        ImGui::TableSetupColumn("outcome");
        ImGui::TableSetupColumn("distance");
        ImGui::TableHeadersRow();
        for (const SightTraceRow& row : Perception.LastTrace())
        {
            if (row.Observer != selected)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u:%u", row.Target.Index, row.Target.Generation);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(OutcomeName(row.Outcome));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", (row.Sample - row.Eye).Magnitude());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
