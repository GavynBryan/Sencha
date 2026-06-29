#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <math/Vec.h>

//=============================================================================
// Collision-shape cook (dev-only, SENCHA_ENABLE_COOK). Pure: no logging, no
// threads, no disk. Bakes a triangle soup into a pre-baked Jolt collision-shape
// blob, the on-disk form the runtime restores directly (no load-time BVH build).
//
// Backend-free signature (positions + indices), so DocumentCook stays Jolt-free
// while feeding the same brush triangles it bakes into the render mesh.
//=============================================================================

// positions: triangle corners; indices: 3 per triangle into positions. Returns
// the serialized shape, or empty with `error` set on a degenerate/empty mesh.
[[nodiscard]] std::vector<std::byte> BakeCollisionBlob(
    std::span<const Vec3d> positions,
    std::span<const uint32_t> indices,
    std::string* error = nullptr);
