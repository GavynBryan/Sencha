#pragma once

#include <assets/cook/BrushClustering.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Static collision triangles
//
// The single source of static collision geometry for every cook product that
// needs to agree on what is solid. The collision bake consumes it per cell;
// the navigation cook consumes the whole zone in world space. Both read the
// same triangles, so a wall that exists physically cannot vanish from
// navigation because two cooks classified geometry differently.
//
// Today every static collider is a brush cell: Collider has no authored form,
// and the runtime creates colliders only from cooked cells. Authored static
// colliders join this source when they gain one.
//=============================================================================

// A triangle soup: every three indices name one triangle's positions.
struct StaticCollisionGeometry
{
    std::vector<Vec3d> Positions;
    std::vector<std::uint32_t> Indices;
    Aabb3d Bounds = Aabb3d::Empty();
};

enum class CollisionTriangleSpace : std::uint8_t
{
    // Positions exactly as the cell stores them, matching the cell's collider
    // entity placed at the cell origin. Copied untouched, so the collision
    // bake's bytes do not depend on floating-point translation.
    CellLocal,
    // Cell origin added to every position.
    World,
};

// Appends one cell's triangles. Vertices are not welded, so triangle order and
// winding follow the cell's faces exactly.
void AppendCellCollisionTriangles(const BrushCell& cell,
                                  CollisionTriangleSpace space,
                                  std::vector<Vec3d>& positions,
                                  std::vector<std::uint32_t>& indices);

// Every cell's triangles in world space, in cell order.
[[nodiscard]] StaticCollisionGeometry
CollectStaticCollisionGeometry(std::span<const BrushCell> cells);
