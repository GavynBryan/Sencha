#pragma once

#include <math/Mat.h>
#include <math/Quat.h>

// A glTF asset faces +Z and the engine faces -Z, so import turns every
// glTF-derived artifact a half turn about +Y (docs/assets/pipeline.md). The
// turn is its own inverse, so an exporter applies the same one.

inline constexpr Quat<float> kGltfToEngineRotation{ 0.0f, 1.0f, 0.0f, 0.0f };

inline constexpr Mat4 GltfToEngineMatrix()
{
    Mat4 turn = Mat4::Identity();
    turn.Data[0][0] = -1.0f;
    turn.Data[2][2] = -1.0f;
    return turn;
}
