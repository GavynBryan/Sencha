#pragma once

#include "LocalAxis.h"

#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>

// How the distance between copies is decided. The default is the relationship
// a level designer means by "repeat this": each copy sits after the previous
// one's extent plus a gap, so widening the source keeps the copies touching
// without anyone retyping an offset. ConstantOffset is the raw translation.
enum class ArrayPlacement : std::uint8_t
{
    RelativeToBounds, // step = input extent along Axis + Spacing (default)
    ConstantOffset,   // step = Offset
};

struct ArrayModifier
{
    ArrayPlacement Placement = ArrayPlacement::RelativeToBounds;
    LocalAxis      Axis      = LocalAxis::X;
    bool           Reverse   = false;            // march along -Axis
    int            Count     = 2;                // includes the source; clamped >= 1
    float          Spacing   = 0.0f;             // empty space between copies (RelativeToBounds)
    Vec3d          Offset    = { 1.0f, 0.0f, 0.0f }; // ConstantOffset only
};

// The translation between consecutive copies, in brush-local space, given the
// local bounds of the piece set the array repeats. After a Mirror that set is
// the mirrored pair, so the pair is the unit that repeats.
[[nodiscard]] Vec3d ResolveArrayStep(const ArrayModifier& array, const Aabb3d& inputBounds);
