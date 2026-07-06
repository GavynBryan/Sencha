#pragma once

#include <cstdint>

enum class MeshElementKind : uint8_t
{
    Object = 0,
    Vertex = 1,
    Edge = 2,
    Face = 3,
};
