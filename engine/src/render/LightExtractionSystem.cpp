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
        bool WantsPointShadow = false;
        SpotShadowView Shadow;
        PointShadowView PointShadow;
        Sphere ShadowBounds;
        std::uint32_t ShadowTileSize = 0;
        ShadowUpdatePolicy ShadowPolicy = ShadowUpdatePolicy::OnChange;
    };

    bool IsUsable(float intensity, float range)
    {
        return std::isfinite(intensity)
            && std::isfinite(range)
            && intensity > 0.0f
            && range > 0.0f;
    }
}

void LightExtractionSystem::Extract(std::span<Registry*> registries,
                                    const CameraRenderData& camera,
                                    RenderLightSet& lights,
                                    std::vector<SpotShadowRequest>& shadowRequests,
                                    std::vector<PointShadowRequest>& pointShadowRequests,
                                    LightExtractionCounts* counts) const
{
    shadowRequests.clear();
    pointShadowRequests.clear();
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
                    // Baked-direct lights contribute only through the zone
                    // lightmap; they never enter the runtime forward set.
                    if (light.BakeContribution == LightBakeContribution::Direct)
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    const Sphere bounds(position, light.Range);
                    if (!camera.ViewFrustum.IntersectsSphere(bounds))
                        return;

                    LightCandidate candidate{
                        .Key = MakeRenderEntityKey(*registry, entity),
                        .Score = LightImportanceScore(
                            position, light.Range, light.Intensity, camera.Position),
                        .Light = MakePointGpuLight(position, light),
                        .WantsPointShadow = light.CastShadows,
                    };
                    if (candidate.WantsPointShadow)
                    {
                        candidate.PointShadow = MakePointShadowView(
                            position, light, lights.ShadowSoftness);
                        candidate.ShadowBounds = bounds;
                        candidate.ShadowPolicy = light.ShadowUpdate;
                    }
                    candidates.push_back(candidate);
                });
        }

        if (world.IsRegistered<SpotLightComponent>())
        {
            world.ForEachComponent<SpotLightComponent>(
                [&](EntityId entity, const SpotLightComponent& light)
                {
                    if (!light.Enabled || !IsUsable(light.Intensity, light.Range))
                        return;
                    // Baked-direct lights contribute only through the zone
                    // lightmap; they never enter the runtime forward set.
                    if (light.BakeContribution == LightBakeContribution::Direct)
                        return;

                    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                    if (transform == nullptr)
                        return;

                    const Vec<3>& position = transform->Value.Position;
                    const Vec<3> direction = transform->Value.Forward();
                    const Sphere bounds = MakeSpotBoundingSphere(
                        position, direction, light.Range, light.OuterAngleDegrees);
                    if (!camera.ViewFrustum.IntersectsSphere(bounds))
                        return;

                    LightCandidate candidate{
                        .Key = MakeRenderEntityKey(*registry, entity),
                        .Score = LightImportanceScore(
                            position, light.Range, light.Intensity, camera.Position),
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
        if (lightIndex == UINT32_MAX)
            continue;

        if (candidate.WantsSpotShadow)
        {
            shadowRequests.push_back(SpotShadowRequest{
                .Key = candidate.Key,
                .LightIndex = lightIndex,
                .Score = candidate.Score,
                .TileSize = candidate.ShadowTileSize,
                .Policy = candidate.ShadowPolicy,
                .StateHash = HashSpotShadowState(candidate.Shadow, candidate.ShadowTileSize),
                .ViewProjection = candidate.Shadow.ViewProjection,
                .SamplingParams = candidate.Shadow.SamplingParams,
                .Bounds = candidate.ShadowBounds,
            });
        }
        else if (candidate.WantsPointShadow)
        {
            pointShadowRequests.push_back(PointShadowRequest{
                .Key = candidate.Key,
                .LightIndex = lightIndex,
                .Score = candidate.Score,
                .Policy = candidate.ShadowPolicy,
                .StateHash = HashPointShadowState(candidate.PointShadow),
                .View = candidate.PointShadow,
                .Bounds = candidate.ShadowBounds,
            });
        }
    }

    if (counts != nullptr)
    {
        counts->FrustumCandidates = static_cast<std::uint32_t>(candidates.size());
        counts->Packed = lights.Count;
    }
}
