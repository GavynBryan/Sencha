#pragma once

#include <math/geometry/3d/Aabb3d.h>

#include <optional>

class EditorScene;

// Union of the world bounds of every boundable entity in the scene; nullopt
// when nothing is boundable (the caller keeps the zone's previous bounds).
// Pure; the world save derives each zone header's bounds through this unless
// the designer overrode them.
[[nodiscard]] std::optional<Aabb3d> ComputeZoneBounds(const EditorScene& scene);
