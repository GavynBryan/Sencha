#include <render/LightSelection.h>

#include <algorithm>
#include <cmath>

bool IsUsableForwardLight(float intensity, float range)
{
    return std::isfinite(intensity) && std::isfinite(range)
        && intensity > 0.0f && range > 0.0f;
}

ForwardLightCandidate MakePointLightCandidate(
    RenderEntityKey key, const Vec<3>& position,
    const PointLightComponent& light, float globalShadowSoftness)
{
    ForwardLightCandidate candidate{
        .Key = key,
        .Position = position,
        .Range = light.Range,
        .Intensity = light.Intensity,
        .Light = MakePointGpuLight(position, light),
        .WantsPointShadow = light.CastShadows,
        .SpotShadow = {},
        .PointShadow = {},
        .ShadowBounds = {},
    };
    if (candidate.WantsPointShadow)
    {
        candidate.PointShadow = MakePointShadowView(
            position, light, globalShadowSoftness);
        candidate.ShadowBounds = Sphere(position, light.Range);
        candidate.ShadowPolicy = light.ShadowUpdate;
    }
    return candidate;
}

ForwardLightCandidate MakeSpotLightCandidate(
    RenderEntityKey key, const Transform3f& transform,
    const SpotLightComponent& light, float globalShadowSoftness)
{
    const Vec<3> direction = transform.Forward();
    ForwardLightCandidate candidate{
        .Key = key,
        .Position = transform.Position,
        .Range = light.Range,
        .Intensity = light.Intensity,
        .Light = MakeSpotGpuLight(transform.Position, direction, light),
        .WantsSpotShadow = light.CastShadows,
        .WantsPointShadow = false,
        .SpotShadow = {},
        .PointShadow = {},
        .ShadowBounds = {},
    };
    if (candidate.WantsSpotShadow)
    {
        candidate.SpotShadow = MakeSpotShadowView(
            transform, light, globalShadowSoftness);
        candidate.ShadowBounds = MakeSpotBoundingSphere(
            transform.Position, direction, light.Range,
            light.OuterAngleDegrees);
        candidate.ShadowTileSize = static_cast<std::uint32_t>(
            light.ShadowResolution);
        candidate.ShadowPolicy = light.ShadowUpdate;
    }
    return candidate;
}

void SelectForwardLights(
    std::vector<ForwardLightCandidate>& candidates,
    const Vec<3>& viewOrigin,
    RenderLightSet& lights,
    std::vector<SpotShadowRequest>& spotRequests,
    std::vector<PointShadowRequest>& pointRequests,
    ForwardLightSelectionCounts* counts)
{
    lights.Reset();
    spotRequests.clear();
    pointRequests.clear();
    std::sort(candidates.begin(), candidates.end(),
        [&viewOrigin](const ForwardLightCandidate& a,
                      const ForwardLightCandidate& b)
        {
            const float aScore = LightImportanceScore(
                a.Position, a.Range, a.Intensity, viewOrigin);
            const float bScore = LightImportanceScore(
                b.Position, b.Range, b.Intensity, viewOrigin);
            if (aScore != bScore)
                return aScore > bScore;
            return a.Key < b.Key;
        });

    const std::size_t count = std::min<std::size_t>(
        candidates.size(), kMaxForwardLights);
    for (std::size_t index = 0; index < count; ++index)
    {
        const ForwardLightCandidate& candidate = candidates[index];
        const float score = LightImportanceScore(
            candidate.Position, candidate.Range, candidate.Intensity, viewOrigin);
        const std::uint32_t lightIndex = lights.Add(candidate.Light);
        if (lightIndex == UINT32_MAX)
            continue;
        if (candidate.WantsSpotShadow)
        {
            spotRequests.push_back(SpotShadowRequest{
                .Key = candidate.Key,
                .LightIndex = lightIndex,
                .Score = score,
                .TileSize = candidate.ShadowTileSize,
                .Policy = candidate.ShadowPolicy,
                .StateHash = HashSpotShadowState(
                    candidate.SpotShadow, candidate.ShadowTileSize),
                .ViewProjection = candidate.SpotShadow.ViewProjection,
                .SamplingParams = candidate.SpotShadow.SamplingParams,
                .Bounds = candidate.ShadowBounds,
            });
        }
        else if (candidate.WantsPointShadow)
        {
            pointRequests.push_back(PointShadowRequest{
                .Key = candidate.Key,
                .LightIndex = lightIndex,
                .Score = score,
                .Policy = candidate.ShadowPolicy,
                .StateHash = HashPointShadowState(candidate.PointShadow),
                .View = candidate.PointShadow,
                .Bounds = candidate.ShadowBounds,
            });
        }
    }

    if (counts != nullptr)
    {
        counts->Candidates = static_cast<std::uint32_t>(candidates.size());
        counts->Packed = lights.Count;
    }
}
