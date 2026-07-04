#pragma once

#include <math/geometry/3d/Aabb3d.h>
#include <zone/WorldPartitionManifest.h>

#include <span>

// The portal's derived facing: the world axis (0/1/2) of minimum extent of its
// brush bounds. A thin box fitted into a wall faces through that wall; nothing
// is stored on the component. Ties resolve to the lowest axis index.
[[nodiscard]] int DominantPortalAxis(const Aabb3d& worldBounds);

// The zone a portal most plausibly opens into: candidates are zones other than
// `owner` whose bounds-center direction from the portal's center is dominant
// along the portal's thin axis; the nearest center wins, ties by ascending
// zone id. Invalid when no candidate (the caller falls back to a plain list).
[[nodiscard]] ZoneId GuessPortalTargetZone(const WorldPartitionManifest& manifest,
                                           ZoneId owner, const Aabb3d& portalWorldBounds);

// A thin axis-aligned box fitted over a face: the face's world AABB expanded
// to `thickness` along the dominant axis of its normal. The axis-aligned
// approximation of "portal in the doorway cut": exact for axis-aligned walls,
// a starting fit for slanted ones (the manipulators finish the job).
struct PortalBoxFit
{
    Vec3d Center;
    Vec3d HalfExtents;
};

[[nodiscard]] PortalBoxFit FitPortalBoxToFace(std::span<const Vec3d> faceWorldVertices,
                                              Vec3d faceNormal, double thickness);
