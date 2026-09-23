#pragma once

#include <math/Mat.h>
#include <math/Quat.h>

//=============================================================================
// glTF frame to engine frame
//
// glTF and the engine agree on right-handed axes with +Y up, and disagree on
// which way an asset faces: a glTF asset's front is +Z, the engine's forward
// is -Z (Vec3::Forward). Every glTF-derived artifact is turned by a half turn
// about +Y at import, through the importer's geometry bake and its skeleton
// fold and nowhere else, so cooked data is in the engine frame and the runtime
// never knows a source was glTF. An exporter writing engine data to glTF
// applies the inverse, which for a half turn is the same turn.
//=============================================================================

inline constexpr Quat<float> kGltfToEngineRotation{ 0.0f, 1.0f, 0.0f, 0.0f };

inline constexpr Mat4 GltfToEngineMatrix()
{
    Mat4 turn = Mat4::Identity();
    turn.Data[0][0] = -1.0f;
    turn.Data[2][2] = -1.0f;
    return turn;
}
