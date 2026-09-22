#include "viewport/OrbitCamera.h"

#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kMaxPitch = 1.5f;
constexpr float kZoomStep = 0.1f;
}

void OrbitCamera::Orbit(float yawDelta, float pitchDelta)
{
    Yaw += yawDelta;
    Pitch = std::clamp(Pitch + pitchDelta, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::Zoom(float steps)
{
    Distance = std::clamp(Distance * std::exp(-steps * kZoomStep), MinDistance, MaxDistance);
}

void OrbitCamera::Frame(const Vec3d& center, float radius)
{
    const float r = std::max(radius, 0.01f);
    Target = center;
    Distance = r * 3.0f;
    MinDistance = r * 0.1f;
    MaxDistance = r * 100.0f;
    Near = r * 0.01f;
    Far = r * 200.0f;
}

Vec3d OrbitCamera::Eye() const
{
    return Target + Vec3d(Distance * std::cos(Pitch) * std::sin(Yaw),
                          Distance * std::sin(Pitch),
                          Distance * std::cos(Pitch) * std::cos(Yaw));
}

CameraRenderData OrbitCamera::BuildRenderData(float aspectRatio) const
{
    CameraRenderData camera;
    camera.Position = Eye();
    camera.View = Mat4::MakeLookAt(camera.Position, Target, Vec3d(0.0f, 1.0f, 0.0f));
    camera.Projection = MakeVulkanPerspective(FovYRadians, aspectRatio > 0.0f ? aspectRatio : 1.0f,
                                              Near, Far);
    camera.ViewProjection = camera.Projection * camera.View;
    camera.ViewFrustum = Frustum::FromViewProjection(camera.ViewProjection);
    return camera;
}
