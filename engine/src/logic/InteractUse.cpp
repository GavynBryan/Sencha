#include <logic/InteractUse.h>

#include <core/metadata/DataSchemaFields.h>
#include <authored/AuthoredValueTraits.h>
#include <ecs/World.h>
#include <world/SimulationAuthority.h>

#include <array>
#include <string>
#include <utility>

namespace
{
} // namespace

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
    VerbDefinition use;
    use.Name = std::string(kInteractUseVerb);
    use.DisplayName = "Use";
    use.Description = "Uses the one thing the activator points at: the first body its aim strikes within range, "
                      "when that entity answers interact.use. Trigger volumes are passed through. Authority only.";
    use.Category = "Interaction";
    use.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "activator", "Activator", "Who is using it, and whose aim is cast."));
    DataFieldSchema range =
        MakeDataField(DataFieldKind::Float, "range", "Range", "How far, in metres, the ray reaches.");
    range.Numeric.Minimum = 0.0;
    use.Arguments.Children.push_back(std::move(range));
    use.Arguments.Children.push_back(DataField::Vector("origin", "Origin", "Where the ray starts. The activator's aim when absent.", 3, false));
    DataFieldSchema direction =
        DataField::Vector("direction", "Direction", "Which way the ray goes. The activator's aim when absent.", 3, false);
    direction.Editor.Widget = "direction";
    use.Arguments.Children.push_back(std::move(direction));
    (void)verbs.Declare(std::move(use));

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

VerbAdmission InteractUseOperations::Use(const VerbInvocation& invocation)
{
    if (!IsSimulationAuthority(*Entities))
        return VerbAdmission::Refused;
    const AuthoredArguments* arguments = invocation.Arguments;
    EntityId activator;
    double range = 0.0;
    if (arguments == nullptr || !arguments->TryGetEntity(0, activator) || !arguments->TryGetFloat(1, range)
        || !(range >= 0.0))
        return VerbAdmission::InvalidArguments;
    if (!Entities->IsAlive(activator))
        return VerbAdmission::Refused;

    Aim ray = AimOf(*Entities, activator);
    (void)ReadAuthoredArgument(*arguments, 2, ray.Origin);
    (void)ReadAuthoredArgument(*arguments, 3, ray.Direction);
    const float length = ray.Direction.Magnitude();
    if (!(length > 1.0e-6f))
        return VerbAdmission::InvalidArguments;
    ray.Direction = ray.Direction / length;

    const UseCandidate candidate = FindUseCandidate(*Responses, Physics, activator, ray, static_cast<float>(range));
    if (!candidate.Target.IsValid() || candidate.Availability.Readiness != ResponseReadiness::Available)
        return VerbAdmission::Refused;
    const std::uint64_t refusedBefore = Responses->Stats().Refused;
    if (Responses->Deliver(candidate.Target, InteractUse{ activator }) > 0)
        return VerbAdmission::Accepted;
    return Responses->Stats().Refused > refusedBefore ? VerbAdmission::QueueFull : VerbAdmission::Refused;
}
