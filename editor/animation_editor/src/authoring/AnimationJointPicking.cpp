#include "authoring/AnimationJointPicking.h"

std::vector<AnimationJointMarker> ProjectAnimationJoints(std::span<const Mat4> model, const Mat4& viewProjection,
                                                         float width, float height)
{
    std::vector<AnimationJointMarker> markers;
    markers.reserve(model.size());
    for (std::size_t j = 0; j < model.size(); ++j)
    {
        const Vec3d origin = model[j].TransformPoint(Vec3d(0.0f, 0.0f, 0.0f));
        const Vec4 clip = viewProjection * Vec4(origin.X, origin.Y, origin.Z, 1.0f);
        if (clip.W <= 1e-6f)
            continue;
        const float x = clip.X / clip.W;
        const float y = clip.Y / clip.W;
        // Vulkan's clip space: y runs down the screen.
        markers.push_back({ static_cast<std::uint32_t>(j), (x * 0.5f + 0.5f) * width, (y * 0.5f + 0.5f) * height,
                            clip.Z / clip.W });
    }
    return markers;
}

std::optional<std::uint32_t> PickAnimationJoint(std::span<const AnimationJointMarker> markers, float x, float y,
                                                float radius)
{
    std::optional<std::uint32_t> picked;
    float best = radius * radius;
    float bestDepth = 0.0f;
    for (const AnimationJointMarker& marker : markers)
    {
        const float dx = marker.X - x;
        const float dy = marker.Y - y;
        const float distance = dx * dx + dy * dy;
        if (distance > radius * radius)
            continue;
        if (!picked || distance < best || (distance == best && marker.Depth < bestDepth))
        {
            picked = marker.Joint;
            best = distance;
            bestDepth = marker.Depth;
        }
    }
    return picked;
}
