#include <perception/PerceptionVocabulary.h>

#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/ObservationMemory.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionStimuli.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <limits>
#include <string>

namespace
{
    DataFieldSchema Argument(std::string key, std::string name, DataFieldKind kind)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.DisplayName = std::move(name);
        field.Kind = kind;
        return field;
    }

    AuthoredQueryDefinition Declaration(std::string_view name, std::string display, std::string description,
                                  DataFieldKind second, std::string secondKey, std::string secondName,
                                  DataFieldKind result)
    {
        AuthoredQueryDefinition definition;
        definition.Name = std::string(name);
        definition.DisplayName = std::move(display);
        definition.Description = std::move(description);
        definition.Category = "Perception";
        definition.Arguments.Children.push_back(Argument("observer", "Observer", DataFieldKind::Entity));
        definition.Arguments.Children.push_back(Argument(std::move(secondKey), std::move(secondName), second));
        definition.Result.Kind = result;
        return definition;
    }

    bool SubjectCarries(const World& world, const GameplayTagRegistry& tags, EntityId subject, GameplayTagId tag)
    {
        const GameplayTagContainer* container = world.TryGet<GameplayTagContainer>(subject);
        return container != nullptr && container->HasDescendantOf(tags, tag);
    }

    // The observer's sight scan this tick, or Unavailable: these queries answer
    // for sight alone, whatever other senses report.
    AuthoredQueryStatus SightScanOf(const World& world, EntityId observer, const PerceptionFrame*& frame,
                                    const SenseScan*& scan)
    {
        frame = world.TryGetResource<PerceptionFrame>();
        const PerceptionSenses* senses = world.TryGetResource<PerceptionSenses>();
        scan = frame != nullptr && senses != nullptr ? frame->ScanOf(observer, senses->Sight) : nullptr;
        return scan != nullptr ? AuthoredQueryStatus::Value : AuthoredQueryStatus::Unavailable;
    }

    // Not seen is an answer only when the sight scan that would have seen it finished.
    AuthoredQueryStatus AnswerUnseen(const SenseScan& scan, AuthoredValue& result)
    {
        if (scan.Coverage != ScanCoverage::Complete)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Bool(false);
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus Sees(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId observer;
        EntityId subject;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetEntity(subject))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = nullptr;
        const SenseScan* scan = nullptr;
        if (SightScanOf(world, observer, frame, scan) != AuthoredQueryStatus::Value)
            return AuthoredQueryStatus::Unavailable;
        for (const Observation& observation : frame->ObservationsOf(*scan))
        {
            if (observation.Subject == subject)
            {
                result = AuthoredValue::Bool(true);
                return AuthoredQueryStatus::Value;
            }
        }
        return AnswerUnseen(*scan, result);
    }

    AuthoredQueryStatus SeesTagged(const World& world, std::span<const AuthoredValue> arguments,
                                   AuthoredValue& result)
    {
        EntityId observer;
        GameplayTagId tag;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = nullptr;
        const SenseScan* scan = nullptr;
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (tags == nullptr || SightScanOf(world, observer, frame, scan) != AuthoredQueryStatus::Value)
            return AuthoredQueryStatus::Unavailable;
        for (const Observation& observation : frame->ObservationsOf(*scan))
        {
            if (SubjectCarries(world, *tags, observation.Subject, tag))
            {
                result = AuthoredValue::Bool(true);
                return AuthoredQueryStatus::Value;
            }
        }
        return AnswerUnseen(*scan, result);
    }

    AuthoredQueryStatus NearestSeenTagged(const World& world, std::span<const AuthoredValue> arguments,
                                          AuthoredValue& result)
    {
        EntityId observer;
        GameplayTagId tag;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = nullptr;
        const SenseScan* scan = nullptr;
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        const WorldTransform* from = world.TryGet<WorldTransform>(observer);
        if (tags == nullptr || from == nullptr
            || SightScanOf(world, observer, frame, scan) != AuthoredQueryStatus::Value)
            return AuthoredQueryStatus::Unavailable;

        EntityId nearest;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (const Observation& observation : frame->ObservationsOf(*scan))
        {
            if (!observation.Subject.IsValid() || !SubjectCarries(world, *tags, observation.Subject, tag))
                continue;
            const float distance = (observation.Position - from->Value.Position).SqrMagnitude();
            if (distance < nearestDistance || (distance == nearestDistance && observation.Subject < nearest))
            {
                nearest = observation.Subject;
                nearestDistance = distance;
            }
        }
        if (!nearest.IsValid())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Entity(nearest);
        return AuthoredQueryStatus::Value;
    }

    const ObservationMemory* MemoryOf(const World& world, EntityId observer)
    {
        return IsSimulationAuthority(world) ? world.TryGet<ObservationMemory>(observer) : nullptr;
    }

    AuthoredQueryStatus Remembered(const World& world, std::span<const AuthoredValue> arguments,
                                   const ObservationMemoryEntry*& entry)
    {
        EntityId observer;
        EntityId subject;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetEntity(subject))
            return AuthoredQueryStatus::InvalidArguments;
        const ObservationMemory* memory = MemoryOf(world, observer);
        if (memory == nullptr)
            return AuthoredQueryStatus::Unavailable;
        entry = subject.IsValid() ? FindRemembered(*memory, subject) : nullptr;
        return AuthoredQueryStatus::Value;
    }

    AuthoredValue VectorValue(const Vec3d& value)
    {
        return AuthoredValue::Vector(AuthoredVectorValue{ .Components = { value.X, value.Y, value.Z, 0.0 },
                                                          .Length = 3 });
    }

    AuthoredQueryStatus Remembers(const World& world, std::span<const AuthoredValue> arguments,
                                  AuthoredValue& result)
    {
        const ObservationMemoryEntry* entry = nullptr;
        const AuthoredQueryStatus status = Remembered(world, arguments, entry);
        if (status == AuthoredQueryStatus::Value)
            result = AuthoredValue::Bool(entry != nullptr);
        return status;
    }

    template<AuthoredValue (*Read)(const ObservationMemoryEntry&)>
    AuthoredQueryStatus RememberedValue(const World& world, std::span<const AuthoredValue> arguments,
                                        AuthoredValue& result)
    {
        const ObservationMemoryEntry* entry = nullptr;
        const AuthoredQueryStatus status = Remembered(world, arguments, entry);
        if (status != AuthoredQueryStatus::Value)
            return status;
        if (entry == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = Read(*entry);
        return AuthoredQueryStatus::Value;
    }

    AuthoredValue PositionOf(const ObservationMemoryEntry& entry) { return VectorValue(entry.Position); }
    AuthoredValue VelocityOf(const ObservationMemoryEntry& entry) { return VectorValue(entry.Velocity); }
    AuthoredValue AgeOf(const ObservationMemoryEntry& entry) { return AuthoredValue::Float(entry.Age); }
    AuthoredValue TrackedOf(const ObservationMemoryEntry& entry) { return AuthoredValue::Float(entry.Tracked); }

    AuthoredQueryStatus NearestRememberedTagged(const World& world, std::span<const AuthoredValue> arguments,
                                                AuthoredValue& result)
    {
        EntityId observer;
        GameplayTagId tag;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const ObservationMemory* memory = MemoryOf(world, observer);
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        const WorldTransform* from = world.TryGet<WorldTransform>(observer);
        if (memory == nullptr || tags == nullptr || from == nullptr)
            return AuthoredQueryStatus::Unavailable;

        EntityId nearest;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (std::uint32_t index = 0; index < memory->Count; ++index)
        {
            const ObservationMemoryEntry& entry = memory->Entries[index];
            if (!entry.Subject.IsValid() || !world.IsAlive(entry.Subject)
                || !SubjectCarries(world, *tags, entry.Subject, tag))
                continue;
            const float distance = (entry.Position - from->Value.Position).SqrMagnitude();
            if (distance < nearestDistance || (distance == nearestDistance && entry.Subject < nearest))
            {
                nearest = entry.Subject;
                nearestDistance = distance;
            }
        }
        if (!nearest.IsValid())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Entity(nearest);
        return AuthoredQueryStatus::Value;
    }

    DataFieldSchema Optional(DataFieldKind kind, std::string key, std::string name, std::string summary)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(name), std::move(summary));
        field.Required = false;
        return field;
    }

    VerbAdmission Stimulate(World& world, const VerbInvocation& invocation)
    {
        PerceptionStimulus stimulus;
        double radius = 0.0;
        if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetFloat(2, radius) || !(radius >= 0.0))
            return VerbAdmission::InvalidArguments;
        stimulus.Radius = static_cast<float>(radius);
        (void)invocation.Arguments->TryGetEntity(0, stimulus.Source);
        (void)invocation.Arguments->TryGetEntity(3, stimulus.Only);
        AuthoredVectorValue position;
        if (invocation.Arguments->TryGetVector(1, position))
        {
            stimulus.Position = Vec3d(static_cast<float>(position.Components[0]), static_cast<float>(position.Components[1]),
                                      static_cast<float>(position.Components[2]));
        }
        else
        {
            const WorldTransform* at =
                stimulus.Source.IsValid() ? world.TryGet<WorldTransform>(stimulus.Source) : nullptr;
            if (at == nullptr)
                return VerbAdmission::InvalidArguments;
            stimulus.Position = at->Value.Position;
        }
        return StagePerceptionStimulus(world, stimulus) ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
}

const PerceptionSenses& EnsurePerceptionSenses(World& world)
{
    if (!world.HasResource<GameplayTagRegistry>())
        world.AddResource<GameplayTagRegistry>();
    if (!world.HasResource<PerceptionSenses>())
    {
        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        const std::optional<GameplayTagId> sight = tags.RegisterTag(kSightSenseTag);
        const std::optional<GameplayTagId> hearing = tags.RegisterTag(kHearingSenseTag);
        world.AddResource<PerceptionSenses>(PerceptionSenses{ .Sight = sight.value_or(GameplayTagId{}),
                                                              .Hearing = hearing.value_or(GameplayTagId{}) });
    }
    return world.GetResource<PerceptionSenses>();
}

bool InstallPerceptionVocabulary(World& world)
{
    (void)EnsurePerceptionSenses(world);
    if (!world.HasResource<PerceptionFrame>())
        world.AddResource<PerceptionFrame>();

    AuthoredQueryRegistry* registry = FindAuthoredQueryRegistry(world);
    if (registry == nullptr)
        return false;
    AuthoredQueryRegistrationScope scope(*registry, "engine");
    (void)scope.Declare(Declaration(kSeesQuery, "Sees",
                              "Whether the observer saw the subject this tick, by sight alone. Unavailable "
                              "when its sight scan did not finish.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Bool));
    (void)scope.Declare(Declaration(kSeesTaggedQuery, "Sees tagged",
                              "Whether the observer saw anything carrying the tag, or a tag beneath it, "
                              "this tick, by sight alone. Unavailable when its sight scan did not finish.",
                              DataFieldKind::GameplayTag, "tag", "Tag", DataFieldKind::Bool));
    (void)scope.Declare(Declaration(kNearestSeenTaggedQuery, "Nearest seen tagged",
                              "The closest thing carrying the tag that the observer saw this tick, by sight alone.",
                              DataFieldKind::GameplayTag, "tag", "Tag", DataFieldKind::Entity));
    (void)scope.Declare(Declaration(kRemembersQuery, "Remembers",
                              "Whether the observer's memory holds the subject.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Bool));
    (void)scope.Declare(Declaration(kLastSeenPositionQuery, "Last seen position",
                              "Where the observer last sensed the subject, not where it is now.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Vector));
    (void)scope.Declare(Declaration(kLastSeenVelocityQuery, "Last seen velocity",
                              "The subject's velocity as the observer last measured it; zero when it could not.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Vector));
    (void)scope.Declare(Declaration(kSecondsSinceSeenQuery, "Seconds since seen",
                              "Seconds since the observer last sensed the subject.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Float));
    (void)scope.Declare(Declaration(kSecondsTrackedQuery, "Seconds tracked",
                              "Seconds the observer has sensed the subject without a lapse.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Float));
    (void)scope.Declare(Declaration(kNearestRememberedTaggedQuery, "Nearest remembered tagged",
                              "The closest remembered position of something alive carrying the tag.",
                              DataFieldKind::GameplayTag, "tag", "Tag", DataFieldKind::Entity));
    if (!scope.Commit())
        return false;

    VerbRegistry* verbs = FindVerbRegistry(world);
    if (verbs == nullptr)
        return false;
    VerbRegistrationScope verbScope(*verbs, "engine");
    VerbDefinition stimulus;
    stimulus.Name = std::string(kPerceptionStimulusVerb);
    stimulus.DisplayName = "Perception stimulus";
    stimulus.Description = "Something hearing picks up: a shot, a footstep, a hit. Heard by every ear within its "
                           "radius, or by one observer alone.";
    stimulus.Category = "Perception";
    stimulus.Arguments.Children.push_back(Optional(DataFieldKind::Entity, "source", "Source",
                                                   "Who made it; an observer remembers them where it happened."));
    DataFieldSchema position = Optional(DataFieldKind::Vector, "position", "Position",
                                        "Where it happened; the source's position without one.");
    position.VectorLength = 3;
    stimulus.Arguments.Children.push_back(std::move(position));
    stimulus.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Float, "radius", "Radius", "Metres it carries to an ear of sensitivity one."));
    stimulus.Arguments.Children.push_back(Optional(DataFieldKind::Entity, "only", "Only",
                                                   "The one observer it reaches, wherever that observer is."));
    (void)verbScope.Declare(std::move(stimulus));
    return verbScope.Commit();
}

VerbBindingToken BindPerceptionVerbs(VerbDispatcher& dispatcher, World& world)
{
    return dispatcher.Bind<&Stimulate>(dispatcher.Registry().Find(kPerceptionStimulusVerb), world);
}

std::vector<AuthoredQueryBindingToken> BindPerceptionQueries(AuthoredQueryDispatcher& dispatcher,
                                                             const World& world)
{
    const AuthoredQueryRegistry& registry = dispatcher.Registry();
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&Sees>(registry.Find(kSeesQuery), world));
    tokens.push_back(dispatcher.Bind<&SeesTagged>(registry.Find(kSeesTaggedQuery), world));
    tokens.push_back(dispatcher.Bind<&NearestSeenTagged>(registry.Find(kNearestSeenTaggedQuery), world));
    tokens.push_back(dispatcher.Bind<&Remembers>(registry.Find(kRemembersQuery), world));
    tokens.push_back(dispatcher.Bind<&RememberedValue<&PositionOf>>(registry.Find(kLastSeenPositionQuery), world));
    tokens.push_back(dispatcher.Bind<&RememberedValue<&VelocityOf>>(registry.Find(kLastSeenVelocityQuery), world));
    tokens.push_back(dispatcher.Bind<&RememberedValue<&AgeOf>>(registry.Find(kSecondsSinceSeenQuery), world));
    tokens.push_back(dispatcher.Bind<&RememberedValue<&TrackedOf>>(registry.Find(kSecondsTrackedQuery), world));
    tokens.push_back(dispatcher.Bind<&NearestRememberedTagged>(registry.Find(kNearestRememberedTaggedQuery), world));
    return tokens;
}
