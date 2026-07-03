#pragma once

#include <ecs/EntityId.h>
#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneId.h>

class CommandStack;
class WorldDocument;

// The transition-creation popup shared by the partition panel (zone row "Add
// Transition To") and the hierarchy panel ("Create Transition From Portal").
// Owns only ImGui transients; every mutation routes through WorldDocument
// verbs plus one undoable link command. Creating a non-OneWay transition
// mints the reverse edge with its own id; nothing stores a pairing field.
class AddTransitionPopup
{
public:
    // Arms the popup to open on the next Draw. Transitions leave `from`; when
    // `linkEntity` names a portal in the focus zone the link box pre-checks.
    void Open(ZoneId from, EntityId linkEntity);

    // Call once per frame at panel scope (not inside another popup).
    void Draw(WorldDocument& world, CommandStack& commands);

private:
    bool Pending_ = false;
    ZoneId From_;
    EntityId LinkEntity_;
    // Target: an existing zone, or (invalid + region set) a zone minted on
    // confirm through the AddZone flow.
    ZoneId TargetZone_;
    RegionId NewZoneRegion_;
    TransitionTopology Topology_ = TransitionTopology::Doorway;
    bool OneWay_ = false;
    int Priority_ = 0;
    bool LinkSelected_ = false;
};
