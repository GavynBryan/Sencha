#pragma once

#include <navigation/tooling/NavMeshSnapshot.h>

#include <math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

// Rooms and passages read off walkable surface, for levels built without
// spaces: a heuristic, labelled one wherever it answers. The method is in
// docs/plans/level-spatial-model.md (M3 progress notes).

struct SpaceProposalSettings
{
    float Cell = 0.25f;      // metres
    float CoreDepth = 1.0f;  // metres from the edge of walkable surface
    float MinRoomArea = 4.0f;
    float MinPassageArea = 1.0f;
    // Walkable surface stops this far short of the walls: the agent's radius.
    float Inset = 0.3f;
    // Floors closer in height than this are one floor.
    float FloorSeparation = 1.0f;
};

enum class ProposedSpaceKind : std::uint8_t
{
    Room,
    Passage,
};

struct ProposedSpace
{
    ProposedSpaceKind Kind = ProposedSpaceKind::Room;
    // The footprint widened by the inset; Min.Y and Max.Y are both the floor.
    Vec3d Min;
    Vec3d Max;
    float Area = 0.0f; // square metres of walkable surface
};

// Over the listed polygons of `snapshot`, rooms first, each kind in order of
// its lowest cell.
[[nodiscard]] std::vector<ProposedSpace> ProposeSpaces(const NavMeshSnapshot& snapshot,
                                                       std::span<const std::uint32_t> polygons,
                                                       const SpaceProposalSettings& settings);
