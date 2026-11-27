#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <core/assets/AssetRef.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>
#include <presentation/PresentationOwner.h>

#include <optional>

#include <string_view>
#include <vector>

class FixedSimulationLoop;
class World;

// The provider. An attachment whose holder dies ends with it, by
// PlaceAttachments, so nothing waits for another acquire or release.
class AttachmentOperations
{
public:
    explicit AttachmentOperations(World& world);

    AttachmentOperations(const AttachmentOperations&) = delete;
    AttachmentOperations& operator=(const AttachmentOperations&) = delete;

    SENCHA_LEASE("presentation.attach")
    SENCHA_LABEL("Hold prop")
    SENCHA_CATEGORY("Presentation")
    SENCHA_DESCRIPTION("Shows a mesh at a socket of the target while held.")
    SENCHA_COSMETIC
    VerbAdmission Attach(
        const VerbInvocation& invocation,
        SENCHA_ARG("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The body that holds it.") EntityId target,
        SENCHA_ARG("socket") SENCHA_LABEL("Socket") SENCHA_DESCRIPTION("The rig socket it is held at, e.g. hand_l.")
            std::string_view socket,
        SENCHA_ARG("mesh") SENCHA_LABEL("Mesh") SENCHA_DESCRIPTION("The static mesh to show.") SENCHA_ASSET(StaticMesh)
            const AssetRef& mesh,
        SENCHA_ARG("material") SENCHA_LABEL("Material") SENCHA_DESCRIPTION("What it draws with.") SENCHA_ASSET(Material)
            const AssetRef& material,
        SENCHA_ARG("position") SENCHA_LABEL("Offset") SENCHA_DESCRIPTION("Metres from the socket, in its space.")
            std::optional<Vec3d> position = std::nullopt,
        SENCHA_ARG("rotation") SENCHA_LABEL("Turn") SENCHA_DESCRIPTION("Pitch, yaw and roll from the socket, in degrees.")
            std::optional<Vec3d> rotation = std::nullopt,
        SENCHA_ARG("cast_shadows") SENCHA_LABEL("Casts shadows") SENCHA_DESCRIPTION("Whether it renders into shadow maps.")
            bool castShadows = true);
    SENCHA_LEASE_RELEASE("presentation.attach")
    VerbAdmission EndAttach(const VerbInvocation& invocation);
    [[nodiscard]] PresentationWriterId Writer() const { return WriterId; }

private:
    struct Held
    {
        LeaseOwner Owner;
        EntityId Attachment;
    };

    VerbAdmission Acquire(const VerbInvocation& invocation, EntityId target, std::string_view socket,
                          const AssetRef& mesh, const AssetRef& material, const Transform3f& offset, bool castShadows);
    void Release(const LeaseOwner& owner);

    World* Entities;
    PresentationWriterId WriterId;
    std::vector<Held> Holds;
};

// The light provider, over the World's AttachedLightState.
class AttachedLightOperations
{
public:
    AttachedLightOperations(World& world, const FixedSimulationLoop& clock);

    AttachedLightOperations(const AttachedLightOperations&) = delete;
    AttachedLightOperations& operator=(const AttachedLightOperations&) = delete;

    SENCHA_LEASE("presentation.light")
    SENCHA_LABEL("Hold light")
    SENCHA_CATEGORY("Presentation")
    SENCHA_DESCRIPTION("Shows a point light on the target while held, then fades it out.")
    SENCHA_COSMETIC
    VerbAdmission Light(
        const VerbInvocation& invocation,
        SENCHA_ARG("target") SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The body that wears it.") EntityId target,
        SENCHA_ARG("socket") SENCHA_LABEL("Socket")
            SENCHA_DESCRIPTION("The rig socket it hangs from; the target's origin when absent or empty.")
                std::optional<std::string_view> socket,
        SENCHA_ARG("position") SENCHA_LABEL("Offset") SENCHA_DESCRIPTION("Metres from the socket, in its space.")
            std::optional<Vec3d> position,
        SENCHA_ARG("color") SENCHA_LABEL("Color") SENCHA_DESCRIPTION("Linear red, green and blue.") Vec3d color,
        SENCHA_ARG("intensity") SENCHA_LABEL("Intensity") SENCHA_DESCRIPTION("Its steady brightness.")
            SENCHA_MIN(0.001) float intensity,
        SENCHA_ARG("range") SENCHA_LABEL("Range") SENCHA_DESCRIPTION("Metres it reaches.") SENCHA_MIN(0.001) float range,
        SENCHA_ARG("peak_scale") SENCHA_LABEL("Peak scale")
            SENCHA_DESCRIPTION("How many times the steady brightness it starts at.") SENCHA_MIN(1) float peakScale = 1.0f,
        SENCHA_ARG("pulse_seconds") SENCHA_LABEL("Pulse")
            SENCHA_DESCRIPTION("How quickly the peak settles to the steady brightness.") SENCHA_MIN(0) SENCHA_UNITS("s")
                float pulseSeconds = 0.0f,
        SENCHA_ARG("attack") SENCHA_LABEL("Attack") SENCHA_DESCRIPTION("Rising from nothing.") SENCHA_MIN(0)
            SENCHA_UNITS("s") float attack = 0.0f,
        SENCHA_ARG("release") SENCHA_LABEL("Release") SENCHA_DESCRIPTION("Fading out once let go.") SENCHA_MIN(0)
            SENCHA_UNITS("s") float release = 0.0f,
        SENCHA_ARG("packs_first") SENCHA_LABEL("Packs first")
            SENCHA_DESCRIPTION("Keeps its place ahead of other live lights when the budget is short.")
                bool packsFirst = false);
    SENCHA_LEASE_RELEASE("presentation.light")
    VerbAdmission EndLight(const VerbInvocation& invocation);

private:
    void Release(const VerbInvocation& invocation);

    World* Entities;
    const FixedSimulationLoop* Clock;
    PresentationWriterId WriterId;
};

#if !defined(SENCHA_CODEGEN)
#  include <attach/AttachmentVerbs.sencha.h>
#endif
