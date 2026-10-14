#pragma once

#include "render/EditorLineSegment.h"

#include <math/geometry/3d/Aabb3d.h>

#include <vector>

// World-space wire shapes for the wide-line pipeline, shared by every overlay
// and stage that outlines one.

void AppendBoxEdges(std::vector<EditorLineSegment>& segments, const Aabb3d& box, const Vec4& color, float widthPx);

// A capsule standing upright about `centre`, `height` tall end to end: a ring at
// its waist and at each cap's base, four lines down its sides, and an arc over
// each cap.
void AppendCapsuleEdges(std::vector<EditorLineSegment>& segments, const Vec3d& centre, float radius, float height,
                        const Vec4& color, float widthPx);

// A square grid on the horizontal plane through `centre`, `halfSize` from its
// middle to each side, a line every `spacing`.
void AppendFloorGrid(std::vector<EditorLineSegment>& segments, const Vec3d& centre, float halfSize, float spacing,
                     const Vec4& color, float widthPx);
