#include <logic/InteractUse.h>

#include <authored/AuthoredApi.h>
#include <ecs/World.h>
#include <world/SimulationAuthority.h>

#include <array>
#include <string>

UseCandidate FindUseCandidate(ResponsesSystem& responses, const PhysicsQueries& physics, EntityId activator,
                              const Aim& ray, float range)
{
    UseCandidate candidate;
    if (!(range > 0.0f))
        return candidate;
    const std::array<EntityId, 1> ignored{ activator };
    const PhysicsQueryFilter filter{ .IncludeTriggers = false, .IgnoreEntities = ignored };
    const RaycastHit hit = physics.Raycast(ray.Origin, ray.Direction, range, filter);
    if (!hit.Hit || !hit.Entity.IsValid())
        return candidate;
    const ResponseAvailability availability = responses.Availability(hit.Entity, InteractUse{ activator });
    if (availability.Readiness == ResponseReadiness::Unanswered)
        return candidate;
    candidate.Target = hit.Entity;
    candidate.Point = hit.Point;
    candidate.Distance = hit.Distance;
    candidate.Availability = availability;
    return candidate;
}

void DeclareInteractUseVocabulary(VerbRegistrationScope& verbs, AuthoredEventRegistrationScope& events)
{
    DeclareAuthoredApi<InteractUseOperations>(&verbs, nullptr);

    AuthoredEventDefinition used;
    used.Name = std::string(kInteractUseEvent);
    used.DisplayName = "Used";
    used.Description = "Someone used this: it was the first thing their aim struck, within their reach.";
    used.Category = "Interaction";
    used.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "activator", "Activator", "Who used it."));
    used.Instigator = "activator";
    (void)events.Declare(std::move(used));
}

InteractUseOperations::InteractUseOperations(const World& world, const PhysicsWorld& physics,
                                             ResponsesSystem& responses)
    : Entities(&world)
    , Physics(physics)
    , Responses(&responses)
{
}

VerbAdmission InteractUseOperations::Use(EntityId activator, float range, std::optional<Vec3d> origin,
                                         std::optional<Vec3d> direction)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    Aim ray = AimOf(*Entities, activator);
    if (origin.has_value())
        ray.Origin = *origin;
    if (direction.has_value())
        ray.Direction = *direction;
    const float length = ray.Direction.Magnitude();
    if (!(length > 1.0e-6f))
        return VerbAdmission::InvalidArguments;
    ray.Direction = ray.Direction / length;

    const UseCandidate candidate = FindUseCandidate(*Responses, Physics, activator, ray, range);
    if (!candidate.Target.IsValid() || candidate.Availability.Readiness != ResponseReadiness::Available)
        return VerbAdmission::Refused;
    const std::uint64_t refusedBefore = Responses->Stats().Refused;
    if (Responses->Deliver(candidate.Target, InteractUse{ activator }) > 0)
        return VerbAdmission::Accepted;
    return Responses->Stats().Refused > refusedBefore ? VerbAdmission::QueueFull : VerbAdmission::Refused;
}
