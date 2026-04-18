#pragma once

#include <math/Mat.h>
#include <math/Vec.h>

// Backend-neutral perspective camera state. Graphics backends consume the
// derived matrices without owning camera policy.
struct PerspectiveCamera
{
    Vec3d Position = Vec3d::Zero();
    Vec3d Target = Vec3d(0.0f, 0.0f, -1.0f);
    Vec3d Up = Vec3d(0.0f, 1.0f, 0.0f);

    float VerticalFovRadians = 1.04719755119659774615f;
    float AspectRatio = 16.0f / 9.0f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;

    [[nodiscard]] Mat4 ViewMatrix() const
    {
        return Mat4::MakeLookAt(Position, Target, Up);
    }

    [[nodiscard]] Mat4 ProjectionMatrix() const
    {
        return Mat4::MakePerspective(VerticalFovRadians, AspectRatio, NearPlane, FarPlane);
    }
};

