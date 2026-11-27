#pragma once

#include <authored/AuthoredAnnotations.h>
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

#include <optional>
#include <string_view>

class PhysicsWorld;
class World;

inline constexpr std::string_view kInteractUseEvent = "interact.use";

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

struct UseCandidate
{
    EntityId Target{};
    Vec3d Point = Vec3d::Zero();
    float Distance = 0.0f;
    ResponseAvailability Availability{};
};

// Read-only; `ray.Direction` is a unit vector.
[[nodiscard]] UseCandidate FindUseCandidate(ResponsesSystem& responses, const PhysicsQueries& physics,
                                            EntityId activator, const Aim& ray, float range);

void DeclareInteractUseVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events);

class InteractUseOperations
{
public:
    InteractUseOperations(const World& world, const PhysicsWorld& physics, ResponsesSystem& responses);

    SENCHA_VERB("interact.use")
    SENCHA_LABEL("Use")
    SENCHA_CATEGORY("Interaction")
    SENCHA_DESCRIPTION("Uses the one thing the activator points at: the first body its aim strikes within range, when "
                       "that entity answers interact.use. Trigger volumes are passed through. Authority only.")
    VerbAdmission Use(SENCHA_TARGET("activator") SENCHA_LABEL("Activator")
                          SENCHA_DESCRIPTION("Who is using it, and whose aim is cast.") EntityId activator,
                      SENCHA_ARG("range") SENCHA_LABEL("Range") SENCHA_DESCRIPTION("How far, in metres, the ray reaches.")
                          SENCHA_MIN(0) float range,
                      SENCHA_ARG("origin") SENCHA_LABEL("Origin")
                          SENCHA_DESCRIPTION("Where the ray starts. The activator's aim when absent.")
                              std::optional<Vec3d> origin = std::nullopt,
                      SENCHA_ARG("direction") SENCHA_LABEL("Direction")
                          SENCHA_DESCRIPTION("Which way the ray goes. The activator's aim when absent.")
                              SENCHA_WIDGET("direction") std::optional<Vec3d> direction = std::nullopt);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    const World* Entities;
    PhysicsQueries Physics;
    ResponsesSystem* Responses;
};

#if !defined(SENCHA_CODEGEN)
#  include <logic/InteractUse.sencha.h>
#endif
