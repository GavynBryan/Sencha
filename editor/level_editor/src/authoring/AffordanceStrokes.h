#pragma once

#include "EditorTheme.h"
#include "authoring/EditorComponentAdapter.h"

#include <math/Vec.h>

// Strokes an adapter draws its affordances with.
void AppendLine(ViewportAffordanceOutput& output, Vec3d a, Vec3d b, Vec4 color,
                float width = EditorTheme::OverlayLinePixels);
void AppendQuad(ViewportAffordanceOutput& output, Vec3d a, Vec3d b, Vec3d c, Vec3d d, Vec4 color);
// A shaft from `from` to `to` with a head whose wings spread along `wingAxis`.
void AppendArrow(ViewportAffordanceOutput& output, Vec3d from, Vec3d to, Vec3d wingAxis, Vec4 color);
