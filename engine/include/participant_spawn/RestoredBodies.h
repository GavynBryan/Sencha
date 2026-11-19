#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <runtime/spawn/SceneSpawnOverlay.h>

#include <optional>
#include <string>

// Where a body stands and what it keeps over its prefab when it comes back.
struct RestoredBody
{
    Transform3f Root = Transform3f::Identity();
    SceneSpawnOverlay Overlay;
};

// World resource: the local participant's next body, taken by its next spawn.
// Every level change clears it before a restore can fill it again.
struct RestoredBodies
{
    std::optional<RestoredBody> Local;
    // Why the spawn that took Local did not land, until the restore reads it.
    std::string Refusal;
};
