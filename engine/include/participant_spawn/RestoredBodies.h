#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <runtime/spawn/SceneSpawnOverlay.h>

#include <optional>

// Where a body stands and what it keeps over its prefab when it comes back.
struct RestoredBody
{
    Transform3f Root = Transform3f::Identity();
    SceneSpawnOverlay Overlay;
};

// World resource: the local participant's next body, taken by its next spawn.
struct RestoredBodies
{
    std::optional<RestoredBody> Local;
};
