#include "authoring/AnimationBlendComparison.h"

#include "authoring/AnimationScenario.h"

#include <core/json/JsonStringify.h>

#include <algorithm>
#include <cmath>

const std::vector<Transform3f>* AnimationPoseTake::At(AnimTick tick) const
{
    const auto it = std::lower_bound(Ticks.begin(), Ticks.end(), tick);
    if (it == Ticks.end() || *it != tick)
        return nullptr;
    return &Poses[static_cast<std::size_t>(it - Ticks.begin())];
}

AnimationPoseTake RecordAnimationPoseTake(const AnimationPreviewSession& session, std::string label)
{
    AnimationPoseTake take;
    take.Label = std::move(label);
    take.ScenarioText = JsonStringify(WriteAnimationScenario(session.Scenario()));
    for (const AnimationPreviewTickRecord& record : session.History())
    {
        if (record.Pose.empty())
            continue;
        take.Ticks.push_back(record.Tick);
        take.Poses.push_back(record.Pose);
    }
    return take;
}

AnimationPoseResidual AnimationPoseDifference(AnimTick tick, const std::vector<Transform3f>& a,
                                              const std::vector<Transform3f>& b)
{
    AnimationPoseResidual residual;
    residual.Tick = tick;
    for (std::size_t j = 0; j < a.size() && j < b.size(); ++j)
    {
        const float distance = (a[j].Position - b[j].Position).Magnitude();
        const float dot = std::min(1.0f, std::abs(a[j].Rotation.Dot(b[j].Rotation)));
        const float angle = 2.0f * std::acos(dot);
        if (distance > residual.Position)
        {
            residual.Position = distance;
            residual.PositionJoint = static_cast<std::uint32_t>(j);
        }
        if (angle > residual.Rotation)
        {
            residual.Rotation = angle;
            residual.RotationJoint = static_cast<std::uint32_t>(j);
        }
    }
    return residual;
}

AnimationPoseComparison CompareAnimationPoseTakes(const AnimationPoseTake& a, const AnimationPoseTake& b)
{
    AnimationPoseComparison comparison;
    if (a.ScenarioText != b.ScenarioText)
    {
        comparison.Refusal = "The takes ran different scenarios, so their difference would not be the edits "
                             "alone. Record A again under the scenario B runs.";
        return comparison;
    }
    for (std::size_t i = 0; i < b.Ticks.size(); ++i)
        if (const std::vector<Transform3f>* pose = a.At(b.Ticks[i]))
            comparison.Residuals.push_back(AnimationPoseDifference(b.Ticks[i], *pose, b.Poses[i]));
    if (comparison.Residuals.empty())
        comparison.Refusal = "The takes share no tick.";
    return comparison;
}
