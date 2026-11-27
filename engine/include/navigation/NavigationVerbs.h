#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <navigation/NavAgent.h>

#include <optional>

class World;

// The verbs give an agent its orders through RequestNavigation and
// CancelNavigation; what becomes of an order is read back with the queries.
class NavigationOperations
{
public:
    explicit NavigationOperations(World& world)
        : Entities(&world)
    {
    }

    SENCHA_VERB("navigation.move_to")
    SENCHA_LABEL("Move to")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Sends the agent to a point, replacing any order it has.")
    VerbAdmission MoveTo(
        SENCHA_TARGET("target", NavAgent) SENCHA_LABEL("Agent") SENCHA_DESCRIPTION("The agent given the order.")
            EntityId target,
        SENCHA_ARG("point") SENCHA_LABEL("Point") SENCHA_DESCRIPTION("Where to go.") Vec3d point,
        SENCHA_ARG("arrive_radius") SENCHA_LABEL("Arrive radius")
            SENCHA_DESCRIPTION("Metres from the point that count as there; zero is the default.") SENCHA_MIN(0)
                float arriveRadius = 0.0f,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("The fraction of its top speed it walks at.")
            SENCHA_MIN(0) float speed = 1.0f);

    SENCHA_VERB("navigation.approach")
    SENCHA_LABEL("Approach")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Sends the agent to an entity, replacing any order it has.")
    VerbAdmission Approach(
        SENCHA_TARGET("target", NavAgent) SENCHA_LABEL("Agent") SENCHA_DESCRIPTION("The agent given the order.")
            EntityId target,
        SENCHA_TARGET("subject") SENCHA_LABEL("Subject")
            SENCHA_DESCRIPTION("What to go to; the order fails if it goes away.") EntityId subject,
        SENCHA_ARG("follow") SENCHA_LABEL("Follow")
            SENCHA_DESCRIPTION("Keep up with it after arriving instead of ending the order.") bool follow = false,
        SENCHA_ARG("arrive_radius") SENCHA_LABEL("Arrive radius")
            SENCHA_DESCRIPTION("Metres from it that count as there; zero is the default.") SENCHA_MIN(0)
                float arriveRadius = 0.0f,
        SENCHA_ARG("speed") SENCHA_LABEL("Speed") SENCHA_DESCRIPTION("The fraction of its top speed it walks at.")
            SENCHA_MIN(0) float speed = 1.0f);

    SENCHA_VERB("navigation.stop")
    SENCHA_LABEL("Stop")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Ends the agent's order; it stands where it is.")
    VerbAdmission Stop(SENCHA_TARGET("target", NavAgent) SENCHA_LABEL("Agent")
                           SENCHA_DESCRIPTION("The agent given the order.") EntityId target);

    SENCHA_QUERY("navigation.phase")
    SENCHA_LABEL("Navigation phase")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Where the agent's latest order stands.")
    std::optional<NavAgentPhase> Phase(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.failure")
    SENCHA_LABEL("Navigation failure")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Why the agent's latest order failed; none while it has not.")
    std::optional<NavAgentFailure> Failure(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.remaining")
    SENCHA_LABEL("Distance remaining")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Metres of route the agent still has to walk.")
    std::optional<float> Remaining(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.crossing_start")
    SENCHA_LABEL("Crossing start")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Where the agent's centre stands to begin the crossing it is making by ability.")
    std::optional<Vec3d> CrossingStart(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.crossing_exit")
    SENCHA_LABEL("Crossing exit")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Where the agent's centre stands at the end of the crossing it is making by ability.")
    std::optional<Vec3d> CrossingExit(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.crossing_anchor")
    SENCHA_LABEL("Crossing anchor")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("The point the crossing it is making by ability hangs from; unavailable when none.")
    std::optional<Vec3d> CrossingAnchor(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    SENCHA_QUERY("navigation.crossing_release")
    SENCHA_LABEL("Crossing release")
    SENCHA_CATEGORY("Navigation")
    SENCHA_DESCRIPTION("Where along its rope the agent lets go to drop onto the exit.")
    std::optional<Vec3d> CrossingRelease(SENCHA_ARG("agent") SENCHA_LABEL("Agent") EntityId agent) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
};

#if !defined(SENCHA_CODEGEN)
#  include <navigation/NavigationVerbs.sencha.h>
#endif
