#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <camera/CameraShake.h>
#include <camera/CameraShakeProfile.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>

class FixedSimulationLoop;
class VerbDispatcher;
class World;

// The shake operations' provider, over the World's CameraShakeState. Cosmetic:
// a presentation runs them on each machine that presents, and nothing in the
// simulation reads what they do. Profiles are the data assets `verbs` checks
// arguments against.
class CameraShakeOperations
{
public:
    CameraShakeOperations(World& world, const FixedSimulationLoop& clock, const VerbDispatcher& verbs)
        : Entities(&world)
        , Clock(&clock)
        , Verbs(&verbs)
    {
    }

    SENCHA_VERB("camera.shake")
    SENCHA_LABEL("Shake camera")
    SENCHA_CATEGORY("Camera")
    SENCHA_DESCRIPTION("Shakes the view once.")
    SENCHA_COSMETIC
    VerbAdmission Shake(const VerbInvocation& invocation,
            SENCHA_ARG("profile") SENCHA_LABEL("Profile") SENCHA_DESCRIPTION("How the view shakes.")
                SENCHA_DATA_ASSET(kCameraShakeType) AuthoredDataRef profile,
            SENCHA_ARG("scale") SENCHA_LABEL("Scale") SENCHA_DESCRIPTION("Multiplies the profile.") SENCHA_MIN(0)
                float scale = 1.0f,
            SENCHA_ARG("subject") SENCHA_LABEL("Subject")
                SENCHA_DESCRIPTION("Only the view this entity owns presents it; every view when unset.")
                    std::optional<EntityId> subject = std::nullopt,
            SENCHA_ARG("point") SENCHA_LABEL("Point")
                SENCHA_DESCRIPTION("Where it comes from, for a profile that falls off with distance.")
                    std::optional<Vec3d> point = std::nullopt,
            SENCHA_ARG("seed") SENCHA_LABEL("Seed") SENCHA_DESCRIPTION("Picks the noise.") std::int64_t seed = 0);

    SENCHA_LEASE("camera.shake.hold")
    SENCHA_LABEL("Hold camera shake")
    SENCHA_CATEGORY("Camera")
    SENCHA_DESCRIPTION("Shakes the view while held, then lets it settle.")
    SENCHA_COSMETIC
    VerbAdmission Hold(const VerbInvocation& invocation,
            SENCHA_ARG("profile") SENCHA_LABEL("Profile") SENCHA_DESCRIPTION("How the view shakes.")
                SENCHA_DATA_ASSET(kCameraShakeType) AuthoredDataRef profile,
            SENCHA_ARG("scale") SENCHA_LABEL("Scale") SENCHA_DESCRIPTION("Multiplies the profile.") SENCHA_MIN(0)
                float scale = 1.0f,
            SENCHA_ARG("subject") SENCHA_LABEL("Subject")
                SENCHA_DESCRIPTION("Only the view this entity owns presents it; every view when unset.")
                    std::optional<EntityId> subject = std::nullopt,
            SENCHA_ARG("point") SENCHA_LABEL("Point")
                SENCHA_DESCRIPTION("Where it comes from, for a profile that falls off with distance.")
                    std::optional<Vec3d> point = std::nullopt,
            SENCHA_ARG("seed") SENCHA_LABEL("Seed") SENCHA_DESCRIPTION("Picks the noise.") std::int64_t seed = 0);
    SENCHA_LEASE_RELEASE("camera.shake.hold")
    VerbAdmission EndHold(const VerbInvocation& invocation);

private:
    VerbAdmission Start(const VerbInvocation& invocation, bool held, AuthoredDataRef profile, float scale,
                        std::optional<EntityId> subject, std::optional<Vec3d> point, std::int64_t seed);

    World* Entities;
    const FixedSimulationLoop* Clock;
    const VerbDispatcher* Verbs;
};

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraShakeVerbs.sencha.h>
#endif
