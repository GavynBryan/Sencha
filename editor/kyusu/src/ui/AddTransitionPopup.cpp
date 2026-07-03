#include "AddTransitionPopup.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "document/commands/LinkPortalCommand.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <utility>

namespace
{

const char* TopologyName(TransitionTopology topology)
{
    switch (topology)
    {
    case TransitionTopology::Seam: return "Seam";
    case TransitionTopology::Doorway: return "Doorway";
    case TransitionTopology::Teleport: return "Teleport";
    }
    return "Doorway";
}

} // namespace

void AddTransitionPopup::Open(ZoneId from, EntityId linkEntity)
{
    Pending_ = true;
    From_ = from;
    LinkEntity_ = linkEntity;
    TargetZone_ = ZoneId{};
    NewZoneRegion_ = RegionId{};
    Topology_ = TransitionTopology::Doorway;
    OneWay_ = false;
    Priority_ = 0;
    LinkSelected_ = linkEntity.IsValid();
}

void AddTransitionPopup::Draw(WorldDocument& world, CommandStack& commands)
{
    if (Pending_)
    {
        ImGui::OpenPopup("##add_transition");
        // Default target: the first zone that is not the source.
        for (const ZoneHeader& zone : world.Manifest().Zones)
            if (zone.Id != From_)
            {
                TargetZone_ = zone.Id;
                break;
            }
        if (!TargetZone_.IsValid() && !world.Manifest().Regions.empty())
            NewZoneRegion_ = world.Manifest().Regions[0].Id;
        Pending_ = false;
    }

    if (!ImGui::BeginPopup("##add_transition"))
        return;

    const auto zoneName = [&](ZoneId zone) -> const std::string*
    {
        for (const ZoneHeader& header : world.Manifest().Zones)
            if (header.Id == zone)
                return &header.Name;
        return nullptr;
    };

    if (const std::string* from = zoneName(From_))
        ImGui::Text("Transition from %s to:", from->c_str());

    std::string preview = "<pick a zone>";
    if (const std::string* target = zoneName(TargetZone_))
        preview = *target;
    else if (NewZoneRegion_.IsValid())
        preview = "New Zone";
    if (ImGui::BeginCombo("##target", preview.c_str()))
    {
        for (const ZoneHeader& zone : world.Manifest().Zones)
        {
            if (zone.Id == From_)
                continue;
            if (ImGui::Selectable(zone.Name.c_str(), zone.Id == TargetZone_))
            {
                TargetZone_ = zone.Id;
                NewZoneRegion_ = RegionId{};
            }
        }
        ImGui::Separator();
        for (const RegionRecord& region : world.Manifest().Regions)
        {
            const std::string label = "New Zone In " + region.Name;
            if (ImGui::Selectable(label.c_str(),
                                  !TargetZone_.IsValid() && region.Id == NewZoneRegion_))
            {
                TargetZone_ = ZoneId{};
                NewZoneRegion_ = region.Id;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Topology", TopologyName(Topology_)))
    {
        for (TransitionTopology topology : { TransitionTopology::Doorway,
                                             TransitionTopology::Seam,
                                             TransitionTopology::Teleport })
            if (ImGui::Selectable(TopologyName(topology), topology == Topology_))
                Topology_ = topology;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("One-Way", &OneWay_);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Preload Priority", &Priority_);

    const bool canLink = LinkEntity_.IsValid()
        && world.FocusZone() == From_
        && world.FocusDocument().GetScene().IsPortal(LinkEntity_);
    if (!canLink)
        LinkSelected_ = false;
    ImGui::BeginDisabled(!canLink);
    ImGui::Checkbox("Link selected portal", &LinkSelected_);
    ImGui::EndDisabled();

    const bool canCreate = TargetZone_.IsValid() || NewZoneRegion_.IsValid();
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create"))
    {
        ZoneId target = TargetZone_;
        if (!target.IsValid())
            target = world.AddZone(NewZoneRegion_, "New Zone");

        const TransitionId forward =
            world.AddTransition(From_, target, Topology_, OneWay_, Priority_);
        if (!OneWay_)
            (void)world.AddTransition(target, From_, Topology_, false, Priority_);

        if (LinkSelected_ && canLink)
        {
            if (auto link = MakeLinkPortalCommand(world.FocusDocument(), LinkEntity_, forward))
            {
                commands.Execute(std::move(link));
                world.Revalidate();
            }
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
