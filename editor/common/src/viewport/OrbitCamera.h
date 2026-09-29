#pragma once

#include <math/Vec.h>
#include <render/extract/Camera.h>

// Yaw and pitch place the eye on a sphere of radius Distance around Target.
struct OrbitCamera
{
    Vec3d Target{};
    float Yaw = 0.6f;
    float Pitch = 0.2f;
    float Distance = 3.0f;

    float MinDistance = 0.1f;
    float MaxDistance = 100.0f;
    float FovYRadians = 0.9f;
    float Near = 0.01f;
    float Far = 200.0f;

    // Pitch stops short of the poles, where the view's up vector would flip.
    void Orbit(float yawDelta, float pitchDelta);

    // Positive steps move nearer, each by the same fraction of the current distance.
    void Zoom(float steps);

    // Fits the distance, zoom range and depth range to the subject; keeps yaw
    // and pitch so reframing never spins the view.
    void Frame(const Vec3d& center, float radius);

    [[nodiscard]] Vec3d Eye() const;
    [[nodiscard]] CameraRenderData BuildRenderData(float aspectRatio) const;
};
