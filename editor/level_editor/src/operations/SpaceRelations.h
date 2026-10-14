#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <vector>

class EditorScene;

// Relationships between spaces and openings, measured from where they are and
// stored nowhere (docs/plans/level-spatial-model.md, invariant 1).

struct WallHit
{
    EntityId Space;
    Vec3d    Outward;
    Vec3d    OnWall; // the point carried onto the middle of the wall
    float    FloorY = 0.0f;
};

// The generated wall a point is in or nearest: a side face of a shelled space
// whose slab, kOpeningReach either way, holds the point, and which the point
// lies over.
[[nodiscard]] std::optional<WallHit> WallAt(const EditorScene& scene, const Vec3d& point);

struct FarSide
{
    EntityId      Space;
    std::uint32_t Face = 0;
    Vec3d         Outward;
};

// The space an opening leads into from `owner`: another space with a face
// looking back along the opening's through axis, within kOpeningReach of the
// opening's middle, which the middle lies over.
[[nodiscard]] std::optional<FarSide> FarSideOf(const EditorScene& scene, EntityId owner, EntityId opening);

// The space an opening belongs to: its parent when that is a space, otherwise
// the space whose wall it stands in.
[[nodiscard]] std::optional<EntityId> OwnerOf(const EditorScene& scene, EntityId opening);

// A volume face in the world: its corners in loop order and outward normal.
struct WorldFace
{
    std::vector<Vec3d> Corners;
    Vec3d              Normal;

    // Whether `point`, carried onto the face's plane, lies within it.
    [[nodiscard]] bool Over(const Vec3d& point) const;
};

[[nodiscard]] std::optional<WorldFace> WorldFaceOf(const EditorScene& scene, EntityId entity, std::uint32_t face);
