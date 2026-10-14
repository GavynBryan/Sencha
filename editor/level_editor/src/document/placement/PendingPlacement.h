#pragma once

#include "PlacementSite.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <string>

// What a placement puts down: a scene source placed as an instance, or a new
// entity carrying one placeable component (none for a bare entity).
struct PlacementSubject
{
    enum class Kind : std::uint8_t
    {
        SceneSource,
        Entity,
    };
    Kind What = Kind::Entity;
    // The scene source's asset path, or the placeable component's JSON key.
    std::string Key;

    bool operator==(const PlacementSubject&) const = default;
};

// One placement as it stands before it is made: what, where, and how it is
// turned, with the transform it will be written with. Whatever shows it and
// whatever commits it read this one value; nothing recomputes it.
struct PendingPlacement
{
    PlacementSubject Subject;
    PlacementSite Site;
    float Yaw = 0.0f;
    Transform3f Placement = Transform3f::Identity();
    // Why the document refuses the subject; empty when it can be placed.
    std::string Refusal;
};
