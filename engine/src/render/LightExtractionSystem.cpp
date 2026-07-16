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
        Sphere ShadowBounds;
        std::uint32_t ShadowTileSize = 0;
        ShadowUpdatePolicy ShadowPolicy = ShadowUpdatePolicy::OnChange;
    };

    constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= kFnvPrime;
        }
    }

    // Hashes every extracted value the rendered depth view depends on; the
    // residency arbiter re-renders an OnChange slot when this changes.
    std::uint64_t HashShadowState(const SpotShadowView& shadow, std::uint32_t tileSize)
    {
        std::uint64_t hash = kFnvOffset;
        HashBytes(hash, &shadow.ViewProjection, sizeof(shadow.ViewProjection));
        HashBytes(hash, &shadow.SamplingParams, sizeof(shadow.SamplingParams));
        HashBytes(hash, &tileSize, sizeof(tileSize));
        return hash;
    }

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
                                    RenderLightSet& lights,
                                    std::vector<SpotShadowRequest>& shadowRequests) const
{
    shadowRequests.clear();
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
                    const Sphere bounds = SpotBounds(
                        position, direction, light.Range, light.OuterAngleDegrees);
                    if (!camera.ViewFrustum.IntersectsSphere(bounds))
                        return;

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
                        candidate.ShadowBounds = bounds;
                        candidate.ShadowTileSize =
                            static_cast<std::uint32_t>(light.ShadowResolution);
                        candidate.ShadowPolicy = light.ShadowUpdate;
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
        if (lightIndex == UINT32_MAX || !candidate.WantsSpotShadow)
            continue;

        shadowRequests.push_back(SpotShadowRequest{
            .Key = candidate.Key,
            .LightIndex = lightIndex,
            .Score = candidate.Score,
            .TileSize = candidate.ShadowTileSize,
            .Policy = candidate.ShadowPolicy,
            .StateHash = HashShadowState(candidate.Shadow, candidate.ShadowTileSize),
            .ViewProjection = candidate.Shadow.ViewProjection,
            .SamplingParams = candidate.Shadow.SamplingParams,
            .Bounds = candidate.ShadowBounds,
        });
    }
}
