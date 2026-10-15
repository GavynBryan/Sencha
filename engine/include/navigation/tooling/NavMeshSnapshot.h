#pragma once

#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>
#include <navigation/NavigationTypes.h>
#include <zone/ZoneId.h>

#include <cstdint>
#include <vector>

class ZoneNavigation;

// One profile's walkable surface as plain data, for tools that draw or measure
// it. Development builds only (SENCHA_ENABLE_COOK); game modules ask through
// NavigationQuery. See docs/navigation/runtime.md, "Tooling access".

struct NavSnapshotPolygon
{
    // Back into the queries, while the zone's navigation generation and the
    // polygon's tile last.
    NavRegion Region;
    // Invalid when the area's name is bound to no gameplay tag.
    GameplayTagId Area;
    // The outline, in loop order, as Vertices[FirstVertex, FirstVertex + VertexCount).
    std::uint32_t FirstVertex = 0;
    std::uint32_t VertexCount = 0;
};

// Where two polygons meet: the stretch of edge they share. From < To.
struct NavSnapshotPortal
{
    std::uint32_t From = 0;
    std::uint32_t To = 0;
    Vec3d A = Vec3d::Zero();
    Vec3d B = Vec3d::Zero();
};

// A polygon edge no other polygon meets: walkable surface ends there.
struct NavSnapshotBorder
{
    std::uint32_t Polygon = 0;
    Vec3d A = Vec3d::Zero();
    Vec3d B = Vec3d::Zero();
};

struct NavMeshSnapshot
{
    ZoneId Zone;
    std::uint32_t Generation = 0;
    std::uint16_t Profile = 0;
    std::vector<Vec3d> Vertices;
    // By tile coordinate, then by polygon within the tile.
    std::vector<NavSnapshotPolygon> Polygons;
    std::vector<NavSnapshotPortal> Portals;
    std::vector<NavSnapshotBorder> Borders;
};

// Allocates in proportion to the mesh: a tool's call, never a frame path.
// Empty for a profile the zone does not have.
[[nodiscard]] NavMeshSnapshot SnapshotNavMesh(const ZoneNavigation& zone, std::uint16_t profile);
