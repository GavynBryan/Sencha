#pragma once

#include <math/Vec.h>
#include <physics/CollisionShape.h>

#include <vector>

// A primitive collider's surface as local-space triangles (three vertices
// each), wound counter-clockwise seen from outside like cooked brush faces.
// Spheres and capsules are coarse hulls: navigation needs their extent, not
// their curvature.
void TriangulateCollider(const CollisionShape& shape, std::vector<Vec3d>& vertices);
