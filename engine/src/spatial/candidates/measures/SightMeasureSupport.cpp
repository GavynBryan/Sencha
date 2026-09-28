#include "SightMeasureSupport.h"

namespace candidate_operation
{
    std::vector<DataFieldSchema> SightFields()
    {
        return { FloatField("half_angle_degrees", "View half-angle (degrees)", 180.0, 0.0),
                 FloatField("range", "Range", std::nullopt, 0.0),
                 FloatField("eye_height", "Eye height", 1.6),
                 FloatField("target_height", "Target height", 1.0) };
    }

    SightArguments ReadSightArguments(const JsonValue& arguments, std::uint8_t slot)
    {
        SightArguments sight;
        sight.Slot = slot;
        sight.HalfAngle = FloatOr(arguments, "half_angle_degrees", 180.0f) * std::numbers::pi_v<float> / 180.0f;
        sight.Range = FloatOr(arguments, "range", std::numeric_limits<float>::infinity());
        sight.EyeHeight = FloatOr(arguments, "eye_height", 1.6f);
        sight.TargetHeight = FloatOr(arguments, "target_height", 1.0f);
        return sight;
    }

    std::optional<SightResult> TestSightCharged(CandidateRun& run,
                                                const SightObserver& observer,
                                                const Vec3d& target,
                                                EntityId targetEntity,
                                                EntityId observerEntity)
    {
        const SightOutcome geometry = TestSightGeometry(observer, target);
        if (geometry != SightOutcome::Seen)
            return SightResult{ .Outcome = geometry };
        if (!run.ChargeRaycast())
            return std::nullopt;
        const EntityId ignore[] = { observerEntity };
        const std::span<const EntityId> ignored =
            observerEntity.IsValid() ? std::span<const EntityId>(ignore) : std::span<const EntityId>{};
        return TestSight(*run.Physics(), observer, target, targetEntity, PhysicsQueryFilter{ .IgnoreEntities = ignored });
    }
}
