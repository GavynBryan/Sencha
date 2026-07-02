#pragma once

#include <render/static_mesh/MeshGeometry.h>

// The material preview's stand-in surfaces. Procedurally built (no asset
// dependency) with full position/normal/uv/tangent streams so every material
// feature (normal maps included) previews correctly.
enum class PreviewPrimitive
{
    Sphere,
    Cube,
    Plane,
    Count
};

[[nodiscard]] const char* PreviewPrimitiveName(PreviewPrimitive kind);

// One section, material slot 0, unit-ish scale centered on the origin.
[[nodiscard]] MeshGeometry BuildPreviewPrimitive(PreviewPrimitive kind);
