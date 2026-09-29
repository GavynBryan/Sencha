#include <anim/AnimBlendspace.h>

#include <anim/AnimFacts.h>

#include <algorithm>

AnimBlendspacePoint AnimBlendspaceCoordinates(const AnimBoundBlendspace& space, std::span<const std::uint32_t> facts,
                                              const AnimBoundRig& rig)
{
    AnimBlendspacePoint at{ 0.0f, 0.0f };
    for (std::size_t a = 0; a < space.AxisCount && a < at.size(); ++a)
    {
        const AnimBoundBlendspaceAxis& axis = space.Axes[a];
        float value = axis.Min;
        if (axis.FactSlot >= 0 && static_cast<std::size_t>(axis.FactSlot) < facts.size())
        {
            const std::uint32_t bits = facts[static_cast<std::size_t>(axis.FactSlot)];
            value = rig.Slots[static_cast<std::size_t>(axis.FactSlot)].Kind == AnimFactKind::Int
                ? static_cast<float>(AnimFactToInt(bits))
                : AnimFactToFloat(bits);
        }
        // A NaN fact places the mix at the axis's start rather than nowhere.
        at[a] = value == value ? std::clamp(value, axis.Min, axis.Max) : axis.Min;
    }
    return at;
}

void AnimBlendspaceWeights(const AnimBoundBlendspace& space, AnimBlendspacePoint at, std::span<float> weights)
{
    const std::size_t count = std::min(space.Samples.size(), weights.size());
    const std::size_t axes = std::min<std::size_t>(space.AxisCount, 2);
    float total = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        float weight = 1.0f;
        const float* pi = space.Samples[i].At;
        for (std::size_t j = 0; j < count && weight > 0.0f; ++j)
        {
            if (j == i)
                continue;
            const float* pj = space.Samples[j].At;
            float along = 0.0f;
            float length = 0.0f;
            for (std::size_t a = 0; a < axes; ++a)
            {
                const float edge = pj[a] - pi[a];
                along += (at[a] - pi[a]) * edge;
                length += edge * edge;
            }
            weight = std::min(weight, std::clamp(1.0f - along / length, 0.0f, 1.0f));
        }
        weights[i] = weight;
        total += weight;
    }
    if (total <= 0.0f)
    {
        // Numerically outside every band: the nearest sample alone.
        std::size_t nearest = 0;
        float best = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            float distance = 0.0f;
            for (std::size_t a = 0; a < axes; ++a)
                distance += (at[a] - space.Samples[i].At[a]) * (at[a] - space.Samples[i].At[a]);
            if (i == 0 || distance < best)
            {
                best = distance;
                nearest = i;
            }
            weights[i] = 0.0f;
        }
        if (count > 0)
            weights[nearest] = 1.0f;
        return;
    }
    for (std::size_t i = 0; i < count; ++i)
        weights[i] /= total;
}

float AnimBlendspaceDuration(const AnimBoundRig& rig, const AnimBoundBlendspace& space, std::span<const float> weights)
{
    float duration = 0.0f;
    for (std::size_t i = 0; i < space.Samples.size() && i < weights.size(); ++i)
    {
        const int content = space.Samples[i].Content;
        if (content >= 0)
            duration += weights[i] * rig.Contents[static_cast<std::size_t>(content)].DurationSeconds;
    }
    return duration;
}

std::size_t AnimBlendspaceDominant(const AnimBoundBlendspace& space, std::span<const float> weights)
{
    std::size_t dominant = 0;
    for (std::size_t i = 1; i < space.Samples.size() && i < weights.size(); ++i)
        if (weights[i] > weights[dominant])
            dominant = i;
    return dominant;
}
