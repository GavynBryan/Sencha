#include <spatial/sight/SightTest.h>

#include <cmath>

namespace
{
    // A hit this close to the target is the target's own surface.
    constexpr float kArrivalTolerance = 0.05f;
}

SightOutcome TestSightGeometry(const SightObserver& observer, const Vec3d& target)
{
    const Vec3d toTarget = target - observer.Eye;
    const float distance = toTarget.Magnitude();
    if (distance > observer.Range)
        return SightOutcome::OutOfRange;

    const float forwardLength = observer.Forward.Magnitude();
    if (observer.HalfAngle < std::numbers::pi_v<float> && forwardLength > 0.0f && distance > 0.0f)
    {
        const float cosine = observer.Forward.Dot(toTarget) / (forwardLength * distance);
        if (cosine < std::cos(observer.HalfAngle))
            return SightOutcome::OutsideCone;
    }
    return SightOutcome::Seen;
}

SightResult TestSight(const PhysicsQueries& physics,
                      const SightObserver& observer,
                      const Vec3d& target,
                      EntityId targetEntity,
                      const PhysicsQueryFilter& filter)
{
    SightResult result;
    result.Outcome = TestSightGeometry(observer, target);
    if (result.Outcome != SightOutcome::Seen)
        return result;

    const Vec3d toTarget = target - observer.Eye;
    const float distance = toTarget.Magnitude();
    if (distance <= kArrivalTolerance)
        return result;

    result.CastRay = true;
    const RaycastHit hit = physics.Raycast(observer.Eye, toTarget / distance, distance, filter);
    const bool reachedTarget = !hit.Hit || hit.Distance >= distance - kArrivalTolerance
        || (targetEntity.IsValid() && hit.Entity == targetEntity);
    if (reachedTarget)
        return result;

    result.Outcome = SightOutcome::Blocked;
    result.HitPoint = hit.Point;
    result.HitEntity = hit.Entity;
    return result;
}
