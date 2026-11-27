#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <camera/CameraEffectTiming.h>
#include <camera/CameraFovScale.h>
#include <ecs/EntityId.h>

#include <optional>

class FixedSimulationLoop;
class World;

// The field of view operations' provider, over the World's CameraFovScaleState.
// Cosmetic, like the shake operations.
class CameraFovScaleOperations
{
public:
    CameraFovScaleOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_VERB("camera.fov_scale")
    SENCHA_LABEL("Scale field of view")
    SENCHA_CATEGORY("Camera")
    SENCHA_DESCRIPTION("Narrows or widens the view for a moment.")
    SENCHA_COSMETIC
    VerbAdmission Pulse(const VerbInvocation& invocation,
            SENCHA_ARG("scale") SENCHA_LABEL("Scale")
                SENCHA_DESCRIPTION("The multiple of the camera's field of view at full strength: below one narrows, above "
                                   "one widens.") SENCHA_RANGE(kCameraFovScaleMin, kCameraFovScaleMax) float scale,
            SENCHA_ARG("attack") SENCHA_LABEL("Attack") SENCHA_DESCRIPTION("Easing to the scale.") SENCHA_MIN(0)
                SENCHA_UNITS("s") float attack = 0.0f,
            SENCHA_ARG("hold") SENCHA_LABEL("Hold") SENCHA_DESCRIPTION("At the scale, when not held by a lease.")
                SENCHA_MIN(0) SENCHA_UNITS("s") float hold = 0.0f,
            SENCHA_ARG("release") SENCHA_LABEL("Release") SENCHA_DESCRIPTION("Easing back to the camera's own.")
                SENCHA_MIN(0) SENCHA_UNITS("s") float release = 0.0f,
            SENCHA_ARG("subject") SENCHA_LABEL("Subject")
                SENCHA_DESCRIPTION("Only the view this entity owns presents it; every view when unset.")
                    std::optional<EntityId> subject = std::nullopt);

    SENCHA_LEASE("camera.fov_scale.hold")
    SENCHA_LABEL("Hold field of view scale")
    SENCHA_CATEGORY("Camera")
    SENCHA_DESCRIPTION("Narrows or widens the view while held, then eases back.")
    SENCHA_COSMETIC
    VerbAdmission Hold(const VerbInvocation& invocation,
            SENCHA_ARG("scale") SENCHA_LABEL("Scale")
                SENCHA_DESCRIPTION("The multiple of the camera's field of view at full strength: below one narrows, above "
                                   "one widens.") SENCHA_RANGE(kCameraFovScaleMin, kCameraFovScaleMax) float scale,
            SENCHA_ARG("attack") SENCHA_LABEL("Attack") SENCHA_DESCRIPTION("Easing to the scale.") SENCHA_MIN(0)
                SENCHA_UNITS("s") float attack = 0.0f,
            SENCHA_ARG("hold") SENCHA_LABEL("Hold") SENCHA_DESCRIPTION("At the scale, when not held by a lease.")
                SENCHA_MIN(0) SENCHA_UNITS("s") float hold = 0.0f,
            SENCHA_ARG("release") SENCHA_LABEL("Release") SENCHA_DESCRIPTION("Easing back to the camera's own.")
                SENCHA_MIN(0) SENCHA_UNITS("s") float release = 0.0f,
            SENCHA_ARG("subject") SENCHA_LABEL("Subject")
                SENCHA_DESCRIPTION("Only the view this entity owns presents it; every view when unset.")
                    std::optional<EntityId> subject = std::nullopt);
    SENCHA_LEASE_RELEASE("camera.fov_scale.hold")
    VerbAdmission EndHold(const VerbInvocation& invocation);

private:
    VerbAdmission Start(const VerbInvocation& invocation, bool held, float scale, const CameraEnvelope& envelope,
                        std::optional<EntityId> subject);

    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraFovScaleVerbs.sencha.h>
#endif
