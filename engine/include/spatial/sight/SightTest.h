#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <physics/PhysicsQueries.h>

#include <cstdint>
#include <limits>
#include <numbers>

// An eye with a view cone and a range. A half-angle of pi or a zero Forward
// sees in every direction.
struct SightObserver
{
    Vec3d Eye = Vec3d::Zero();
    Vec3d Forward = Vec3d::Zero();
    float HalfAngle = std::numbers::pi_v<float>;
    float Range = std::numeric_limits<float>::infinity();
};

enum class SightOutcome : std::uint8_t
{
    Seen,
    OutOfRange,
    OutsideCone,
    Blocked,
};

struct SightResult
{
    SightOutcome Outcome = SightOutcome::Blocked;
    // Only a Blocked result casts a ray that hit something.
    Vec3d HitPoint = Vec3d::Zero();
    EntityId HitEntity{};
    bool CastRay = false;

    [[nodiscard]] bool Seen() const { return Outcome == SightOutcome::Seen; }
};

// Rejects by range, then cone, and only then casts, so cheap rejections spend
// no ray. The target counts as seen when the ray reaches it clear or first hits
// `targetEntity` itself.
[[nodiscard]] SightResult TestSight(const PhysicsQueries& physics,
                                    const SightObserver& observer,
                                    const Vec3d& target,
                                    EntityId targetEntity,
                                    const PhysicsQueryFilter& filter);

// The range and cone checks alone, for callers that must know whether a ray
// would be spent before spending it.
[[nodiscard]] SightOutcome TestSightGeometry(const SightObserver& observer, const Vec3d& target);
