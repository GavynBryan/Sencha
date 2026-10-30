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

inline constexpr std::string_view kInteractUseVerb = "interact.use";
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
