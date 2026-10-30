#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <controller/Aim.h>
#include <ecs/EntityId.h>
#include <logic/ResponsesSystem.h>
#include <math/Vec.h>
#include <physics/PhysicsQueries.h>

#include <string_view>

class PhysicsWorld;
class World;

//=============================================================================
// interact.use
//
// A conventional Use: the one entity an activator points at answers it. The
// verb casts a ray -- the activator's aim, unless the invocation names an origin
// and a direction -- takes the first body it strikes within range, and delivers
// the interact.use occurrence there when that entity's responses answer it.
// Trigger volumes neither block the ray nor answer it, and nothing behind the
// first body is considered: a wall in front of a terminal is what the ray
// strikes, so the terminal is not used. What using a thing does is the thing's
// own responses (docs/gameplay/responses.md, "Use").
//
// Nothing here knows what was pressed. An ability binds an input action to an
// invoke of this verb, as any other press-and-do-something is bound, and the
// ability pass is what decides a press happened, once, on the authority
// (docs/gameplay/abilitykit.md). The area form is trigger.interact, which
// answers every volume the activator stands in, and is untouched by this one.
//
// The verb and the occurrence share a name; verbs and events are separate
// catalogs, and a binding names one as "verb" and the other as "on".
//=============================================================================

inline constexpr std::string_view kInteractUseVerb = "interact.use";
inline constexpr std::string_view kInteractUseEvent = "interact.use";

// Delivered at the entity used. The activator is its instigator, so a binding's
// commands are aimed at whoever used it by default.
struct InteractUse
{
    EntityId Activator{};
};

template <>
struct AuthoredApiDefinition<InteractUse>
{
    static constexpr std::string_view EventName = kInteractUseEvent;

    static void Encode(const InteractUse& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Entity(event.Activator));
    }
};

// What a use ray found. Target is invalid when the ray struck nothing within
// range, or struck something whose responses do not answer interact.use: a
// wall, the floor, a body no entity owns.
struct UseCandidate
{
    EntityId Target{};
    Vec3d Point = Vec3d::Zero();
    float Distance = 0.0f;
    // How far this process can tell the target would answer; on a client a
    // provisional answer (ResponseAvailability::RuntimeStateKnown).
    ResponseAvailability Availability{};
};

// The candidate along `ray` (its direction a unit vector) within `range`, for
// `activator` to use: read-only, so it runs no verb, queues nothing and changes
// no binding. The verb and a prompt both ask this, so what a prompt points at is
// what a press would reach for.
[[nodiscard]] UseCandidate FindUseCandidate(ResponsesSystem& responses, const PhysicsQueries& physics,
                                            EntityId activator, const Aim& ray, float range);

// The interact.use verb and the interact.use occurrence.
void DeclareInteractUseVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);

class InteractUseOperations
{
public:
    InteractUseOperations(const World& world, const PhysicsWorld& physics, ResponsesSystem& responses);

    // Arguments, in order: activator, range, origin and direction (both
    // optional: the activator's aim). Refused on a client, for a dead activator,
    // and when nothing in range answers -- a zero range, or a target whose
    // bindings are all blocked; QueueFull when the response queue refused it;
    // InvalidArguments for a negative range or a direction of no length.
    // Accepted once the occurrence was admitted.
    VerbAdmission Use(const VerbInvocation& invocation);

private:
    const World* Entities;
    PhysicsQueries Physics;
    ResponsesSystem* Responses;
};

inline VerbAdmission InvokeInteractUse(InteractUseOperations& operations, const VerbInvocation& invocation)
{
    return operations.Use(invocation);
}
