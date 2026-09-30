#pragma once

#include "EditorOverlayState.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/Vec.h>

#include <string>
#include <vector>

// Format a unit length for an on-screen readout: whole numbers without a decimal,
// otherwise two places.
[[nodiscard]] std::string FormatUnits(double value);

// One width/length/height label per non-degenerate axis of a world AABB, each
// anchored at the midpoint of the box edge running along that axis from the min
// corner. Pure (no scene), so it is unit-tested directly.
[[nodiscard]] std::vector<LabelRequest> SelectionDimensionLabels(const Aabb3d& bounds, const Vec4& color);

// Appends the same labels into caller-owned storage. The overlay rebuilds its
// label list every frame, so appending into a retained vector keeps that
// per-frame work allocation-free once the capacity has settled.
void AppendSelectionDimensionLabels(const Aabb3d& bounds, const Vec4& color,
                                    std::vector<LabelRequest>& out);
