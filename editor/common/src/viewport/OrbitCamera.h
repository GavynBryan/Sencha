#pragma once

#include <math/Vec.h>
#include <render/extract/Camera.h>

//=============================================================================
// OrbitCamera
//
// A camera that circles a subject: the preview viewports' camera, where the
// thing being looked at is fixed and the author turns it over. Yaw and pitch
// place the eye on a sphere around Target; Distance is the sphere's radius.
//
// Pure math over plain values, so framing and zoom limits are testable without
// a device. What it draws and how its input arrives stay with each preview.
//=============================================================================
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

    // Turns the eye around the target. Pitch stops short of the poles, where
    // the view's up vector would flip.
    void Orbit(float yawDelta, float pitchDelta);

    // Wheel steps in, positive nearer. Exponential, so each step is the same
    // fraction of the current distance at any scale.
    void Zoom(float steps);

    // Aims at a subject of the given radius from a distance that fits it, and
    // scales the zoom range and depth range to it. Yaw and pitch are kept, so
    // reframing after a content change does not spin the view.
    void Frame(const Vec3d& center, float radius);

    [[nodiscard]] Vec3d Eye() const;
    [[nodiscard]] CameraRenderData BuildRenderData(float aspectRatio) const;
};
