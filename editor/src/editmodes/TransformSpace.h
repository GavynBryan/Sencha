#pragma once

#include <cstdint>

// The frame the transform gizmos draw and drag along. Grid follows the
// workspace grid frame (identical to World until the grid is moved/rotated);
// Local follows the primary selected entity's rotation.
enum class TransformSpace : uint8_t
{
    Grid = 0,
    World = 1,
    Local = 2,
};

[[nodiscard]] constexpr const char* TransformSpaceLabel(TransformSpace space)
{
    switch (space)
    {
    case TransformSpace::Grid:  return "Grid";
    case TransformSpace::World: return "World";
    case TransformSpace::Local: return "Local";
    }
    return "?";
}

[[nodiscard]] constexpr TransformSpace NextTransformSpace(TransformSpace space)
{
    switch (space)
    {
    case TransformSpace::Grid:  return TransformSpace::World;
    case TransformSpace::World: return TransformSpace::Local;
    case TransformSpace::Local: return TransformSpace::Grid;
    }
    return TransformSpace::Grid;
}
