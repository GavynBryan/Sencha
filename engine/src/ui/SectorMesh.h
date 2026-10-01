#pragma once

#include <math/Vec.h>

#include <cstdint>
#include <vector>

// Radians clockwise from straight up. `Inset` pulls every edge in by that many
// pixels, the flanks parallel to themselves: what a stroke inside the edge
// leaves for the fill.
struct SectorShape
{
    Vec2d Center = {};
    float Outer = 0.0f;
    float Inner = 0.0f;
    float Start = 0.0f;
    float Sweep = 0.0f;
    float Inset = 0.0f;
};

// Coverage is one inside and falls to zero across a fringe `feather` pixels
// wide centred on every edge; scaling a colour's alpha by it antialiases the
// shape without multisampling.
struct SectorMesh
{
    std::vector<Vec2d> Positions;
    std::vector<float> Coverage;
    std::vector<std::uint32_t> Indices;
};

// Nothing is appended when the inset leaves no shape.
void AppendSectorMesh(const SectorShape& shape, float feather, SectorMesh& mesh);
