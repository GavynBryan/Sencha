#pragma once

#include <math/Quat.h>
#include <render/Camera.h>

struct EditorCamera
{
    enum class Mode
    {
        Perspective,
        Orthographic
    };

    Mode ActiveMode = Mode::Perspective;

    Vec3d Position = { 0.0f, 2.0f, -5.0f };
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float FovY = 60.0f;
    float MoveSpeed = 5.0f;

    float OrthoHeight = 20.0f;
    Vec3d OrthoCenter = {};
    Vec3d OrthoAxis = { 0.0f, 1.0f, 0.0f };
    // Explicit view-up for the ortho basis. Zero keeps the world-up heuristic;
    // the workspace sets it from the grid frame so ortho views stay aligned to
    // a moved/rotated working grid (the grid is never diagonal on screen).
    Vec3d OrthoUpHint = {};

    float Near = 0.01f;
    float Far = 10000.0f;

    [[nodiscard]] CameraRenderData BuildRenderData(float aspectRatio) const;
    [[nodiscard]] Quatf GetRotation() const;
    [[nodiscard]] Vec3d GetForwardVector() const;
    [[nodiscard]] Vec3d GetRightVector() const;
    [[nodiscard]] Vec3d GetUpVector() const;
    [[nodiscard]] Vec3d GetOrthoRightVector() const;
    [[nodiscard]] Vec3d GetOrthoUpVector() const;
    void ApplyPerspectiveLook(float deltaX, float deltaY);
    void ApplyPerspectiveMove(Vec3d moveVector, float deltaSeconds);
    void ApplyPerspectiveDolly(float amount);
    void ApplyOrthoPan(float deltaX, float deltaY, float viewportHeight);
    void ApplyOrthoZoom(float steps);
};
