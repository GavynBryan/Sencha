#pragma once

#include "BrushMesh.h"
#include "LocalAxis.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Plane.h>

// Where a Mirror's plane comes from. The default is derived from the brush
// origin so the architectural workflow is "add Mirror X, put the origin on the
// symmetry line": moving the origin moves the copy. Only a Custom plane is an
// independent authored value that survives a re-origin in place.
enum class MirrorPlaneSource : std::uint8_t
{
    Origin,       // through the brush origin, normal along Axis (default)
    BoundsCenter, // through the center of what the stack has produced so far
    Custom,       // an independently authored local plane, any normal
};

struct MirrorModifier
{
    LocalAxis         Axis   = LocalAxis::X;
    MirrorPlaneSource Source = MirrorPlaneSource::Origin;
    float             Offset = 0.0f; // along Axis from the source point (Origin, BoundsCenter)
    Plane             CustomPlane = Plane::FromNormalAndDistance(Vec3d{ 1.0f, 0.0f, 0.0f }, 0.0f);
};

// The plane a Mirror reflects across, in brush-local space, given the local
// bounds of the piece set it is applied to (only BoundsCenter reads them).
[[nodiscard]] Plane ResolveMirrorPlane(const MirrorModifier& mirror, const Aabb3d& inputBounds);

// The vector `v` reflected across a plane with unit normal `n` (direction only:
// a translation conjugated through the reflection).
[[nodiscard]] Vec3d ReflectVector(Vec3d n, Vec3d v);

// The point reflected across `plane` (normalized).
[[nodiscard]] Vec3d ReflectPoint(const Plane& plane, Vec3d p);

// The mesh reflected across `localPlane`: positions reflected, every loop
// reversed so faces stay outward, normals recomputed, soft edges kept, and each
// face's texture projection rewritten by MirrorFaceProjection so the copy shows
// the source's image un-reflected. Vertex and face indices are the source's:
// this never welds, drops, or repairs, which is what lets a face on the copy
// stand for the same-index face on the source. A plane with a degenerate
// normal returns the mesh unchanged.
[[nodiscard]] BrushMesh MirrorBrushMesh(const BrushMesh& mesh, const Plane& localPlane);
