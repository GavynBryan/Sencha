#include <render/ProjectedShadowDirection.h>

#include <algorithm>
#include <cmath>

namespace
{

// The forward shader's attenuation shape (lighting.glsli): inverse-square
// times the (d/r)^4 window, cone term for spots. Matching it means the
// direction agrees with what the eye sees lighting the caster.
float LightWeightAt(const GpuLight& light, const Vec<3>& point)
{
    if (light.Type == static_cast<std::uint32_t>(GpuLightType::Directional))
        return light.ColorIntensity.W;

    const Vec<3> toLight = Vec<3>(light.PositionRange.X - point.X,
                                  light.PositionRange.Y - point.Y,
                                  light.PositionRange.Z - point.Z);
    const float distanceSquared = toLight.Dot(toLight);
    const float distance = std::sqrt(distanceSquared);

    float cone = 1.0f;
    if (light.Type == static_cast<std::uint32_t>(GpuLightType::Spot))
    {
        const Vec<3> lightDirection = toLight * (1.0f / std::max(distance, 1e-4f));
        const float coneCosine = -(lightDirection.X * light.DirectionCone.X
                                   + lightDirection.Y * light.DirectionCone.Y
                                   + lightDirection.Z * light.DirectionCone.Z);
        cone = std::clamp(coneCosine * light.ConeScale + light.ConeOffset, 0.0f, 1.0f);
        cone *= cone;
    }

    const float rangeRatio = distance / std::max(light.PositionRange.W, 1e-4f);
    const float ratioSquared = rangeRatio * rangeRatio;
    const float window = std::clamp(1.0f - ratioSquared * ratioSquared, 0.0f, 1.0f);

    return light.ColorIntensity.W * cone * (window * window)
         / (distanceSquared + 1e-4f);
}

Vec<3> DirectionFrom(const GpuLight& light, const Vec<3>& point)
{
    if (light.Type == static_cast<std::uint32_t>(GpuLightType::Directional))
    {
        // A directional light's stored direction is the direction it shines.
        return Vec<3>(light.DirectionCone.X, light.DirectionCone.Y,
                      light.DirectionCone.Z);
    }
    // Shadows fall away from the light: from the light through the caster.
    Vec<3> away = Vec<3>(point.X - light.PositionRange.X,
                         point.Y - light.PositionRange.Y,
                         point.Z - light.PositionRange.Z);
    const float length = std::sqrt(away.Dot(away));
    return length > 1e-5f ? away * (1.0f / length) : Vec<3>(0.0f, -1.0f, 0.0f);
}

Vec<3> Normalized(const Vec<3>& v, const Vec<3>& fallback)
{
    const float length = std::sqrt(v.Dot(v));
    return length > 1e-5f ? v * (1.0f / length) : fallback;
}

} // namespace

Vec<3> ProjectedShadowTargetDirection(std::span<const GpuLight> lights,
                                      const Vec<3>& casterCenter,
                                      const ProjectedShadowDirectionParams& params)
{
    // Weights squared: the strongest light dominates visibly while the blend
    // stays continuous as lights move, fade, or swap rank.
    Vec<3> sum = params.FallbackDirection * params.FallbackWeight;
    for (const GpuLight& light : lights)
    {
        const float weight = LightWeightAt(light, casterCenter);
        sum = sum + DirectionFrom(light, casterCenter) * (weight * weight);
    }
    return Normalized(sum, params.FallbackDirection);
}

void UpdateProjectedShadowDirections(ProjectedShadowSet& set,
                                     std::span<const GpuLight> lights,
                                     std::vector<ProjectedShadowDirectionState>& state,
                                     float dt,
                                     const ProjectedShadowDirectionParams& params)
{
    const float blend = 1.0f - std::exp(-params.SmoothingRate * std::max(dt, 0.0f));

    for (ProjectedShadowCaster& caster : set.Casters)
    {
        const Vec<3> target = ProjectedShadowTargetDirection(
            lights, caster.WorldBounds.Center(), params);

        const auto it = std::lower_bound(
            state.begin(), state.end(), caster.Key,
            [](const ProjectedShadowDirectionState& entry, const RenderEntityKey& key)
            { return entry.Key < key; });

        if (it != state.end() && it->Key == caster.Key)
        {
            // nlerp: normalized blend of unit directions. Enough for
            // grounding -- slerp's constant angular velocity buys nothing a
            // blob shadow can show.
            it->Direction = Normalized(
                it->Direction + (target - it->Direction) * blend,
                params.FallbackDirection);
            it->UnseenFrames = 0;
            caster.Direction = it->Direction;
        }
        else
        {
            // First sight: no history to smooth from, start at the target.
            state.insert(it, ProjectedShadowDirectionState{
                .Key = caster.Key,
                .Direction = target,
                .UnseenFrames = 0,
            });
            caster.Direction = target;
        }
    }

    // Age everything not refreshed above; evict the long-unseen so the state
    // tracks the population that grounds, not everything that ever did.
    for (ProjectedShadowDirectionState& entry : state)
        ++entry.UnseenFrames;
    std::erase_if(state, [&](const ProjectedShadowDirectionState& entry)
                  { return entry.UnseenFrames > params.EvictAfterFrames; });
}
