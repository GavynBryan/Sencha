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
        const LightCandidate& candidate = candidates[index];
        const std::uint32_t lightIndex = lights.Add(candidate.Light);
        if (lightIndex != UINT32_MAX && candidate.WantsSpotShadow)
            (void)lights.GrantSpotShadow(lightIndex, candidate.Shadow);
    }
}
