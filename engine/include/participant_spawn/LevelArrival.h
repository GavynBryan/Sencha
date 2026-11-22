#pragma once

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <optional>
#include <string>
#include <vector>

// A body a level change kept, held still until the new level says where it stands.
struct KeptArrival
{
    EntityId Participant;
    // Set by the game's body spawns once the level can answer; the level change moves the body there.
    std::optional<Transform3f> Place;
};

// World resource: where bodies arrive after the level change under way or last carried out.
struct LevelArrival
{
    std::string Name;
    std::vector<KeptArrival> Kept;
};
