#include <render/LightExtractionSystem.h>

#include <math/geometry/3d/Sphere.h>
#include <render/PointLightComponent.h>
#include <render/RenderEntityKey.h>
#include <render/SpotLightComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    struct LightCandidate
    {
        RenderEntityKey Key;
        float Score = 0.0f;
        GpuLight Light;
        bool WantsSpotShadow = false;
        SpotShadowView Shadow;
    };

    float LightScore(const Vec<3>& position,
                     float range,
                     float intensity,
                     const CameraRenderData& camera)
    {
        const float distance = Vec<3>::Distance(position, camera.Position);
        const float reach = std::clamp(range / std::max(distance, 1.0e-4f), 0.0f, 1.0f);
        return intensity * reach * reach;
    }

    bool IsUsable(float intensity, float range)
    {
        return std::isfinite(intensity)
            && std::isfinite(range)
            && intensity > 0.0f
            && range > 0.0f;
    }

    Sphere SpotBounds(const Vec<3>& position,
                      const Vec<3>& direction,
                      float range,
                      float outerAngleDegrees)
    {
        constexpr float degreesToRadians = 0.01745329251994329577f;
        const float angle = std::clamp(outerAngleDegrees, 0.01f, 89.9f)
                          * degreesToRadians;
        const float halfRange = range * 0.5f;
        const float coneRadius = range * std::tan(angle);
        const float radius = std::sqrt(halfRange * halfRange + coneRadius * coneRadius);
        return Sphere(position + direction * halfRange, radius);
    }

    Mat4 MakeSpotProjection(float outerAngleDegrees, float nearPlane, float farPlane)
    {
        constexpr float degreesToRadians = 0.01745329251994329577f;
        const float fov = std::clamp(outerAngleDegrees * 2.0f, 0.02f, 179.8f)
                        * degreesToRadians;
        const float tanHalfFov = std::tan(fov * 0.5f);
        Mat4 result;
        result[0][0] = 1.0f / tanHalfFov;
        result[1][1] = -1.0f / tanHalfFov;
        result[2][2] = farPlane / (nearPlane - farPlane);
        result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
        result[3][2] = -1.0f;
        return result;
    }

    SpotShadowView MakeSpotShadowView(
        const Transform3f& worldTransform,
        const SpotLightComponent& light,
        float globalSoftness)
    {
        constexpr float degreesToRadians = 0.01745329251994329577f;
        const float outerAngle = std::clamp(light.OuterAngleDegrees, 0.01f, 89.9f)
                               * degreesToRadians;
        const float nearPlane = std::max(0.05f, light.Range * 0.02f);
        const Transform3f lightTransform(
            worldTransform.Position,
            worldTransform.Rotation,
            Vec3d(1.0f, 1.0f, 1.0f));
        const Mat4 view = lightTransform.ToMat4().AffineInverse();
        const Mat4 projection = MakeSpotProjection(
            light.OuterAngleDegrees, nearPlane, light.Range);

        SpotShadowView shadow;
        shadow.ViewProjection = projection * view;
        const float texelWorldSize =
            2.0f * light.Range * std::tan(outerAngle)
            / static_cast<float>(kSpotShadowInnerExtent);
        const float softness = std::clamp(
            light.ShadowSoftness * globalSoftness,
            kSpotShadowSoftnessMinTexels,
            kSpotShadowSoftnessMaxTexels);
        shadow.SamplingParams = Vec4(
            texelWorldSize,
            softness,
            std::max(light.ShadowBiasScale, 0.0f),
            0.0f);
        return shadow;
    }
}

void LightExtractionSystem::Extract(std::span<Registry*> registries,
                                    const CameraRenderData& camera,
                                    RenderLightSet& lights) const
{
    std::vector<LightCandidate> candidates;

    for (Registry* registry : registries)
    {
        if (registry == nullptr)
            continue;

        const World& world = registry->Components;
        if (!world.IsRegistered<WorldTransform>())
            continue;

        if (world.IsRegistered<PointLightComponent>())
        {
            world.ForEachComponent<PointLightComponent>(
                [&](EntityId entity, const PointLightComponent& light)
                {
                    if (!light.Enabled || !IsUsable(light.Intensity, light.Range))
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    if (!camera.ViewFrustum.IntersectsSphere(Sphere(position, light.Range)))
                        return;

                    candidates.push_back(LightCandidate{
                        .Key = MakeRenderEntityKey(*registry, entity),
                        .Score = LightScore(position, light.Range, light.Intensity, camera),
                        .Light = MakePointGpuLight(position, light),
                    });
                });
        }

        if (world.IsRegistered<SpotLightComponent>())
        {
            world.ForEachComponent<SpotLightComponent>(
                [&](EntityId entity, const SpotLightComponent& light)
                {
                    if (!light.Enabled || !IsUsable(light.Intensity, light.Range))
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    const Vec<3> direction = transform->Value.Forward();
                    if (!camera.ViewFrustum.IntersectsSphere(
                            SpotBounds(position, direction, light.Range,
                                       light.OuterAngleDegrees)))
                    {
                        return;
                    }

                    LightCandidate candidate{
                        .Key = MakeRenderEntityKey(*registry, entity),
                        .Score = LightScore(position, light.Range, light.Intensity, camera),
                        .Light = MakeSpotGpuLight(position, direction, light),
                        .WantsSpotShadow = light.CastShadows,
                    };
                    if (candidate.WantsSpotShadow)
                    {
                        candidate.Shadow = MakeSpotShadowView(
                            transform->Value, light, lights.ShadowSoftness);
                    }
                    candidates.push_back(candidate);
                });
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const LightCandidate& a, const LightCandidate& b)
        {
            if (a.Score != b.Score)
                return a.Score > b.Score;
            return a.Key < b.Key;
        });

    const std::size_t count = std::min<std::size_t>(
        candidates.size(), kMaxForwardLights);
    for (std::size_t index = 0; index < count; ++index)
    {
        LightCandidate candidate = candidates[index];
        const std::uint32_t lightIndex = lights.Add(candidate.Light);
        if (lightIndex == UINT32_MAX
            || !candidate.WantsSpotShadow
            || lights.SpotShadowCount >= kMaxSpotShadows)
        {
            continue;
        }

        const std::uint32_t shadowIndex = lights.SpotShadowCount++;
        candidate.Shadow.LightIndex = lightIndex;
        candidate.Shadow.AtlasScaleBias = SpotShadowAtlasScaleBias(shadowIndex);
        lights.SpotShadows[shadowIndex] = candidate.Shadow;
        lights.Lights[lightIndex].ShadowIndex = shadowIndex;
    }
}
