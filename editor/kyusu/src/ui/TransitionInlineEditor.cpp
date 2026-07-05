#include "TransitionInlineEditor.h"

#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"

#include <imgui.h>

#include <cstring>
#include <string>

void DrawTransitionInlineEditor(WorldDocument& world, TransitionId transition,
                                TransitionId partner)
{
    const TransitionRecord* record = nullptr;
    for (const TransitionRecord& candidate : world.Manifest().Transitions)
        if (candidate.Id == transition)
            record = &candidate;
    if (record == nullptr)
        return;

    const auto both = [&](auto&& apply)
    {
        apply(transition);
        if (partner.IsValid())
            apply(partner);
    };

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
                both([&](TransitionId id) { (void)world.SetTransitionTopology(id, topology); });
        ImGui::EndCombo();
    }

    int priority = record->PreloadPriority;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Priority", &priority))
        both([&](TransitionId id) { (void)world.SetTransitionPreloadPriority(id, priority); });
    ImGui::SameLine();
    int depth = record->PreloadDepth;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Depth", &depth))
        both([&](TransitionId id) { (void)world.SetTransitionPreloadDepth(id, depth); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Preload reach through this connection (0 = the global horizon)");

    // Gate: the connection exists for streaming only while ALL listed world
    // tags are active. Committed on deactivate so typing does not spam verbs.
    char tagBuffer[256];
    const std::string joined = JoinTagList(record->RequiredTags);
    std::strncpy(tagBuffer, joined.c_str(), sizeof(tagBuffer) - 1);
    tagBuffer[sizeof(tagBuffer) - 1] = '\0';
    ImGui::SetNextItemWidth(-1.0f);
    (void)ImGui::InputText("##required_tags", tagBuffer, sizeof(tagBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit())
        both([&](TransitionId id)
             { (void)world.SetTransitionRequiredTags(id, SplitTagList(tagBuffer)); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Required world tags (comma separated); empty = always open");
}
