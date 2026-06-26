#include "EditorCamera.h"

#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegreesToRadians = kPi / 180.0f;
constexpr float kMouseSensitivity = 0.0025f;
constexpr float kMaxPitchRadians = 1.5f;
constexpr float kPerspectiveDollyScale = 2.0f;
constexpr float kOrthoZoomStep = 0.9f;
constexpr float kMinOrthoHeight = 0.25f;
constexpr float kMaxOrthoHeight = 10000.0f;

void GetOrthoBasis(const Vec3d& orthoAxis, Vec3d& right, Vec3d& up)
{
    const Vec3d forward = (-orthoAxis).Normalized();
    Vec3d viewUp = Vec3d::Up();
    if (std::abs(orthoAxis.Dot(viewUp)) > 0.999f)
        viewUp = Vec3d::Forward();

    right = forward.Cross(viewUp).Normalized();
    up = right.Cross(forward).Normalized();
}

Mat4 MakeVulkanPerspective(float fovYRadians,
                           float aspect,
                           float nearPlane,
                           float farPlane)
{
    const float tanHalfFov = std::tan(fovYRadians * 0.5f);
    Mat4 result;
    result[0][0] = 1.0f / (aspect * tanHalfFov);
    result[1][1] = -1.0f / tanHalfFov;
    result[2][2] = farPlane / (nearPlane - farPlane);
    result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    result[3][2] = -1.0f;
    return result;
}

Mat4 MakeVulkanOrthographic(float left,
                            float right,
                            float bottom,
                            float top,
                            float nearPlane,
                            float farPlane)
{
    Mat4 result = Mat4::Identity();
    result[0][0] = 2.0f / (right - left);
    result[1][1] = -2.0f / (top - bottom);
    result[2][2] = 1.0f / (nearPlane - farPlane);
    result[0][3] = -(right + left) / (right - left);
    result[1][3] = -(top + bottom) / (top - bottom);
    result[2][3] = nearPlane / (nearPlane - farPlane);
    return result;
}
}

CameraRenderData EditorCamera::BuildRenderData(float aspectRatio) const
{
    CameraRenderData data;
    data.Position = ActiveMode == Mode::Perspective ? Position : OrthoCenter + OrthoAxis * 100.0f;

    if (ActiveMode == Mode::Perspective)
    {
        const Vec3d target = Position + GetForwardVector();
        data.View = Mat4::MakeLookAt(Position, target, GetUpVector());
        data.Projection = MakeVulkanPerspective(FovY * kDegreesToRadians, aspectRatio, Near, Far);
    }
    else
    {
        const Vec3d eye = OrthoCenter + OrthoAxis * 100.0f;
        Vec3d up = Vec3d::Up();
        if (std::abs(OrthoAxis.Dot(up)) > 0.999f)
            up = Vec3d::Forward();

        const float halfHeight = OrthoHeight * 0.5f;
        const float halfWidth = halfHeight * aspectRatio;
        data.View = Mat4::MakeLookAt(eye, OrthoCenter, up);
        data.Projection = MakeVulkanOrthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, Near, Far);
    }

    data.ViewProjection = data.Projection * data.View;
    data.ViewFrustum = Frustum::FromViewProjection(data.ViewProjection);
    return data;
}

Quatf EditorCamera::GetRotation() const
{
    return Quatf::FromAxisAngle(Vec3d::Up(), Yaw)
        * Quatf::FromAxisAngle(Vec3d::Right(), Pitch);
}

Vec3d EditorCamera::GetForwardVector() const
{
    return GetRotation().RotateVector(Vec3d::Forward());
}

Vec3d EditorCamera::GetRightVector() const
{
    return GetRotation().RotateVector(Vec3d::Right());
}

Vec3d EditorCamera::GetUpVector() const
{
    return GetRotation().RotateVector(Vec3d::Up());
}

Vec3d EditorCamera::GetOrthoRightVector() const
{
    Vec3d right;
    Vec3d up;
    GetOrthoBasis(OrthoAxis, right, up);
    return right;
}

Vec3d EditorCamera::GetOrthoUpVector() const
{
    Vec3d right;
    Vec3d up;
    GetOrthoBasis(OrthoAxis, right, up);
    return up;
}

void EditorCamera::ApplyPerspectiveLook(float deltaX, float deltaY)
{
    if (ActiveMode != Mode::Perspective)
        return;

    Yaw -= deltaX * kMouseSensitivity;
    Pitch -= deltaY * kMouseSensitivity;
    if (Pitch > kMaxPitchRadians)
        Pitch = kMaxPitchRadians;
    if (Pitch < -kMaxPitchRadians)
        Pitch = -kMaxPitchRadians;
}

void EditorCamera::ApplyPerspectiveMove(Vec3d moveVector, float deltaSeconds)
{
    if (ActiveMode != Mode::Perspective || moveVector.SqrMagnitude() <= 0.0f)
        return;

    Position += GetRotation().RotateVector(moveVector.Normalized()) * (MoveSpeed * deltaSeconds);
}

void EditorCamera::ApplyPerspectiveDolly(float amount)
{
    if (ActiveMode != Mode::Perspective || amount == 0.0f)
        return;

    Position += GetForwardVector() * (amount * MoveSpeed * kPerspectiveDollyScale);
}

void EditorCamera::ApplyOrthoPan(float deltaX, float deltaY, float viewportHeight)
{
    if (ActiveMode != Mode::Orthographic || viewportHeight <= 0.0f)
        return;

    const float worldUnitsPerPixel = OrthoHeight / viewportHeight;
    OrthoCenter -= GetOrthoRightVector() * (deltaX * worldUnitsPerPixel);
    OrthoCenter += GetOrthoUpVector() * (deltaY * worldUnitsPerPixel);
}

void EditorCamera::ApplyOrthoZoom(float steps)
{
    if (ActiveMode != Mode::Orthographic || steps == 0.0f)
        return;

    OrthoHeight *= std::pow(kOrthoZoomStep, steps);
    if (OrthoHeight < kMinOrthoHeight)
        OrthoHeight = kMinOrthoHeight;
    if (OrthoHeight > kMaxOrthoHeight)
        OrthoHeight = kMaxOrthoHeight;
}
