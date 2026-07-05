#include "TransitionInlineEditor.h"

#include "document/WorldDocument.h"

#include <imgui.h>

void DrawTransitionInlineEditor(WorldDocument& world, TransitionId transition)
{
    const TransitionRecord* record = nullptr;
    for (const TransitionRecord& candidate : world.Manifest().Transitions)
        if (candidate.Id == transition)
            record = &candidate;
    if (record == nullptr)
        return;

    const auto topologyName = [](TransitionTopology topology)
    {
        switch (topology)
        {
        case TransitionTopology::Seam: return "Seam";
        case TransitionTopology::Doorway: return "Doorway";
        case TransitionTopology::Teleport: return "Teleport";
        }
        return "Doorway";
    };

    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("Topology", topologyName(record->Topology)))
    {
        for (TransitionTopology topology : { TransitionTopology::Doorway,
                                             TransitionTopology::Seam,
                                             TransitionTopology::Teleport })
            if (ImGui::Selectable(topologyName(topology), topology == record->Topology))
                (void)world.SetTransitionTopology(transition, topology);
        ImGui::EndCombo();
    }

    bool oneWay = record->Flags.OneWay;
    if (ImGui::Checkbox("One-way", &oneWay))
        (void)world.SetTransitionOneWay(transition, oneWay);
    ImGui::SameLine();
    int priority = record->PreloadPriority;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Priority", &priority))
        (void)world.SetTransitionPreloadPriority(transition, priority);
}
