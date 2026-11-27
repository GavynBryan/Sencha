#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/LeaseOwner.h>
#include <ecs/Query.h>
#include <math/Vec.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/HeldMotion.h>

#include <optional>
#include <string_view>

class World;

// The movement operations' provider. Each lease holds one of a character's motion
// slots (movement/components/HeldMotion.h) for its owner, and lets it go; a timed
// burst completes when its time is up, and a tether when its anchor goes. A launch
// is instant: it writes the forced motion channels for the coming composition.
class MovementOperations
{
public:
    explicit MovementOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_LEASE("movement.burst")
    SENCHA_LABEL("Burst of motion")
    SENCHA_CATEGORY("Movement")
    SENCHA_DESCRIPTION("Drives the character at a velocity while held, or for a time.")
    SENCHA_PREDICTION(Simulation)
    SENCHA_COMPLETES
    VerbAdmission Burst(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target", CharacterMovement) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The character it holds.")
            EntityId target,
        SENCHA_ARG("direction") SENCHA_LABEL("Direction") SENCHA_DESCRIPTION("Which way, as aim.direction gives it.")
            SENCHA_WIDGET("direction") Vec3d direction,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("Metres per second of the character's time.")
            SENCHA_MIN(0) float speed = 10.0f,
        SENCHA_ARG("seconds") SENCHA_LABEL("Seconds") SENCHA_DESCRIPTION("How long; zero holds it until let go.")
            SENCHA_MIN(0) float seconds = 0.0f,
        SENCHA_ARG("rises") SENCHA_LABEL("Rises") SENCHA_DESCRIPTION("Its upward part lifts the character too.")
            bool rises = false);
    SENCHA_LEASE_RELEASE("movement.burst")
    VerbAdmission EndBurst(const VerbInvocation& invocation);
    SENCHA_LEASE_HOLDS("movement.burst")
    bool BurstHolds(const LeaseOwner& owner) const;

    SENCHA_LEASE("movement.tether")
    SENCHA_LABEL("Tether")
    SENCHA_CATEGORY("Movement")
    SENCHA_DESCRIPTION("Pulls the character toward an anchor once it is farther away than the rope.")
    SENCHA_PREDICTION(Simulation)
    SENCHA_COMPLETES
    VerbAdmission Tether(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target", CharacterMovement) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The character it holds.")
            EntityId target,
        SENCHA_ARG("anchor") SENCHA_LABEL("Anchor") SENCHA_DESCRIPTION("An entity to follow; it ends the tether when it goes.")
            std::optional<EntityId> anchor = std::nullopt,
        SENCHA_ARG("point") SENCHA_LABEL("Point") SENCHA_DESCRIPTION("Where the anchor is, when no entity is.")
            std::optional<Vec3d> point = std::nullopt,
        SENCHA_ARG("length") SENCHA_LABEL("Length") SENCHA_DESCRIPTION("Metres of rope.") SENCHA_MIN(0)
            float length = 2.0f,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("How fast it pulls, in metres per second.")
            SENCHA_MIN(0) float speed = 12.0f,
        SENCHA_ARG("until") SENCHA_LABEL("Until")
            SENCHA_DESCRIPTION("A point along the rope: the length becomes its distance from the anchor, and reaching "
                               "it stops the character and completes the tether.")
                std::optional<Vec3d> until = std::nullopt);
    SENCHA_LEASE_RELEASE("movement.tether")
    VerbAdmission EndTether(const VerbInvocation& invocation);
    SENCHA_LEASE_HOLDS("movement.tether")
    bool TetherHolds(const LeaseOwner& owner) const;

    SENCHA_LEASE("movement.mode")
    SENCHA_LABEL("Locomotion mode")
    SENCHA_CATEGORY("Movement")
    SENCHA_DESCRIPTION("Puts the character in a locomotion mode while held.")
    SENCHA_PREDICTION(Simulation)
    VerbAdmission Mode(
        const VerbInvocation& invocation,
        SENCHA_TARGET("target", CharacterMovement) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The character it holds.")
            EntityId target,
        SENCHA_ARG("mode") SENCHA_LABEL("Mode") SENCHA_DESCRIPTION("The mode's name, as movement.mode.<name>.")
            std::string_view mode);
    SENCHA_LEASE_RELEASE("movement.mode")
    VerbAdmission EndMode(const VerbInvocation& invocation);

    SENCHA_VERB("movement.launch")
    SENCHA_LABEL("Launch")
    SENCHA_CATEGORY("Movement")
    SENCHA_DESCRIPTION("Sets the character moving at a velocity, whatever it was moving at; it keeps that momentum "
                       "afterwards.")
    VerbAdmission Launch(
        SENCHA_TARGET("target", CharacterMovement) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The character launched.")
            EntityId target,
        SENCHA_ARG("direction") SENCHA_LABEL("Direction") SENCHA_DESCRIPTION("Which way, in world space.")
            SENCHA_WIDGET("direction") Vec3d direction,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("Metres per second along the direction.")
            SENCHA_MIN(0) float speed = 10.0f,
        SENCHA_ARG("rise") SENCHA_LABEL("Rise")
            SENCHA_DESCRIPTION("Metres per second added straight up, along the character's up axis.") SENCHA_MIN(0)
                float rise = 0.0f);

    SENCHA_VERB("movement.impulse")
    SENCHA_LABEL("Impulse")
    SENCHA_CATEGORY("Movement")
    SENCHA_DESCRIPTION("Adds a velocity to the character's, on top of whatever it was moving at: a shove.")
    VerbAdmission Impulse(
        SENCHA_TARGET("target", CharacterMovement) SENCHA_LABEL("Target") SENCHA_DESCRIPTION("The character shoved.")
            EntityId target,
        SENCHA_ARG("direction") SENCHA_LABEL("Direction") SENCHA_DESCRIPTION("Which way, in world space.")
            SENCHA_WIDGET("direction") Vec3d direction,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("Metres per second along the direction.")
            SENCHA_MIN(0) float speed = 1.0f,
        SENCHA_ARG("rise") SENCHA_LABEL("Rise")
            SENCHA_DESCRIPTION("Metres per second added straight up, along the character's up axis.") SENCHA_MIN(0)
                float rise = 0.0f);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    // Whether `owner` still holds a slot of `T`, looked for on its own entity first.
    template <typename T>
    [[nodiscard]] bool Holds(std::optional<Query<Read<T>>>& everyone, const LeaseOwner& owner) const;
    VerbAdmission Velocity(EntityId target, Vec3d direction, float speed, float rise, Vec3d& velocity, Vec3d& up) const;

    World* Entities;
    mutable std::optional<Query<Read<MotionBursts>>> Bursts;
    mutable std::optional<Query<Read<TetherSession>>> Tethers;
};

#if !defined(SENCHA_CODEGEN)
#  include <movement/MovementVerbs.sencha.h>
#endif
