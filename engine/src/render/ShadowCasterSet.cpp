#include <render/ShadowCasterSet.h>

#include <algorithm>
#include <cmath>

namespace
{
    // 1/16 world unit: coarse enough to absorb transform-propagation float
    // noise, far below any spot range worth invalidating over.
    constexpr float kBoundsQuantum = 16.0f;

    [[nodiscard]] float Quantize(float value)
    {
        return std::round(value * kBoundsQuantum) / kBoundsQuantum;
    }
}

Aabb3d QuantizeShadowCasterBounds(const Aabb3d& bounds)
{
    return Aabb3d(
        Vec3d(Quantize(bounds.Min.X), Quantize(bounds.Min.Y), Quantize(bounds.Min.Z)),
        Vec3d(Quantize(bounds.Max.X), Quantize(bounds.Max.Y), Quantize(bounds.Max.Z)));
}

void ShadowCasterDiff::Apply(std::vector<ShadowCasterRecord>& current,
                             bool emitEvents,
                             std::vector<ShadowCasterEvent>& events)
{
    std::sort(current.begin(), current.end(),
        [](const ShadowCasterRecord& a, const ShadowCasterRecord& b)
        {
            return a.Key < b.Key;
        });

    if (emitEvents)
    {
        std::size_t previousIndex = 0;
        std::size_t currentIndex = 0;
        while (previousIndex < Previous.size() || currentIndex < current.size())
        {
            if (previousIndex == Previous.size())
            {
                events.push_back(ShadowCasterEvent{
                    .Change = ShadowCasterEvent::Kind::Added,
                    .Bounds = current[currentIndex].State.WorldBounds,
                });
                ++currentIndex;
                continue;
            }
            if (currentIndex == current.size())
            {
                events.push_back(ShadowCasterEvent{
                    .Change = ShadowCasterEvent::Kind::Removed,
                    .Bounds = Previous[previousIndex].State.WorldBounds,
                });
                ++previousIndex;
                continue;
            }

            const ShadowCasterRecord& previous = Previous[previousIndex];
            const ShadowCasterRecord& record = current[currentIndex];
            if (previous.Key < record.Key)
            {
                events.push_back(ShadowCasterEvent{
                    .Change = ShadowCasterEvent::Kind::Removed,
                    .Bounds = previous.State.WorldBounds,
                });
                ++previousIndex;
            }
            else if (record.Key < previous.Key)
            {
                events.push_back(ShadowCasterEvent{
                    .Change = ShadowCasterEvent::Kind::Added,
                    .Bounds = record.State.WorldBounds,
                });
                ++currentIndex;
            }
            else
            {
                if (!(previous.State == record.State))
                {
                    events.push_back(ShadowCasterEvent{
                        .Change = ShadowCasterEvent::Kind::Changed,
                        .Bounds = previous.State.WorldBounds.ExpandedToInclude(
                            record.State.WorldBounds),
                    });
                }
                ++previousIndex;
                ++currentIndex;
            }
        }
    }

    Previous = std::move(current);
    current.clear();
}
