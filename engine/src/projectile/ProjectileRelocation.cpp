#include <projectile/ProjectileRegistration.h>

#include <world/relocation/RelocationRules.h>

namespace
{
    void TurnVelocity(ProjectileMotion& motion, const RelocationFrame& frame)
    {
        motion.Velocity = frame.MapVector(motion.Velocity);
    }
}

void RegisterProjectileRelocation(World& world)
{
    SetRelocationRule<ProjectileMotion>(world, RelocationRuleKind::Maps,
                                        &RelocateValue<ProjectileMotion, &TurnVelocity>);
}
