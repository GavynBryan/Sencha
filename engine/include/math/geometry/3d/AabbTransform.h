#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>

// Separate from Aabb3d.h on purpose. That header is included nearly everywhere
// and deliberately depends on Vec alone; pulling Mat.h into it puts the Mat
// class template in scope for every one of those translation units, which is
// enough to make an unrelated local helper named Mat ambiguous.

// The axis-aligned bounds of `local` after `world` is applied: the eight
// transformed corners re-bounded, so a rotation grows the box rather than
// shearing it.
//
// Every renderable's world bounds come from here. Three copies of this loop
// existed -- mesh extraction, shadow-caster extraction, and the editor's queue
// builder -- which is three chances for culling to disagree with itself about
// where the same object is.
[[nodiscard]] Aabb3d TransformAabb(const Aabb3d& local, const Mat4& world);
