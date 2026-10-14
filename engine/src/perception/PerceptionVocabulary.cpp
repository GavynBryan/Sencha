#include <perception/PerceptionVocabulary.h>

#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/PerceptionFrame.h>
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

    // Not seen is an answer only when the scan that would have seen it finished.
    AuthoredQueryStatus AnswerUnseen(const PerceptionFrame& frame, EntityId observer, AuthoredValue& result)
    {
        if (frame.CoverageOf(observer) != ScanCoverage::Complete)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Bool(false);
        return AuthoredQueryStatus::Value;
    }

    bool SubjectCarries(const World& world, const GameplayTagRegistry& tags, EntityId subject, GameplayTagId tag)
    {
        const GameplayTagContainer* container = world.TryGet<GameplayTagContainer>(subject);
        return container != nullptr && container->HasDescendantOf(tags, tag);
    }

    AuthoredQueryStatus Sees(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId observer;
        EntityId subject;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetEntity(subject))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = world.TryGetResource<PerceptionFrame>();
        if (frame == nullptr)
            return AuthoredQueryStatus::Unavailable;
        if (frame->Find(observer, subject) == nullptr)
            return AnswerUnseen(*frame, observer, result);
        result = AuthoredValue::Bool(true);
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus SeesTagged(const World& world, std::span<const AuthoredValue> arguments,
                                   AuthoredValue& result)
    {
        EntityId observer;
        GameplayTagId tag;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = world.TryGetResource<PerceptionFrame>();
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (frame == nullptr || tags == nullptr)
            return AuthoredQueryStatus::Unavailable;
        for (const SenseScan& scan : frame->ScansOf(observer))
        {
            for (const Observation& observation : frame->ObservationsOf(scan))
            {
                if (SubjectCarries(world, *tags, observation.Subject, tag))
                {
                    result = AuthoredValue::Bool(true);
                    return AuthoredQueryStatus::Value;
                }
            }
        }
        return AnswerUnseen(*frame, observer, result);
    }

    AuthoredQueryStatus NearestSeenTagged(const World& world, std::span<const AuthoredValue> arguments,
                                          AuthoredValue& result)
    {
        EntityId observer;
        GameplayTagId tag;
        if (!arguments[0].TryGetEntity(observer) || !arguments[1].TryGetTag(tag))
            return AuthoredQueryStatus::InvalidArguments;
        const PerceptionFrame* frame = world.TryGetResource<PerceptionFrame>();
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        const WorldTransform* from = world.TryGet<WorldTransform>(observer);
        if (frame == nullptr || tags == nullptr || from == nullptr)
            return AuthoredQueryStatus::Unavailable;

        EntityId nearest;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (const SenseScan& scan : frame->ScansOf(observer))
        {
            for (const Observation& observation : frame->ObservationsOf(scan))
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
        }
        if (!nearest.IsValid())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Entity(nearest);
        return AuthoredQueryStatus::Value;
    }
}

const PerceptionSenses& EnsurePerceptionSenses(World& world)
{
    if (!world.HasResource<GameplayTagRegistry>())
        world.AddResource<GameplayTagRegistry>();
    if (!world.HasResource<PerceptionSenses>())
    {
        const std::optional<GameplayTagId> sight =
            world.GetResource<GameplayTagRegistry>().RegisterTag(kSightSenseTag);
        world.AddResource<PerceptionSenses>(PerceptionSenses{ .Sight = sight.value_or(GameplayTagId{}) });
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
                              "Whether the observer saw the subject this tick. Unavailable when its scan "
                              "did not finish.",
                              DataFieldKind::Entity, "subject", "Subject", DataFieldKind::Bool));
    (void)scope.Declare(Declaration(kSeesTaggedQuery, "Sees tagged",
                              "Whether the observer saw anything carrying the tag, or a tag beneath it, "
                              "this tick. Unavailable when its scan did not finish.",
                              DataFieldKind::GameplayTag, "tag", "Tag", DataFieldKind::Bool));
    (void)scope.Declare(Declaration(kNearestSeenTaggedQuery, "Nearest seen tagged",
                              "The closest thing carrying the tag that the observer saw this tick.",
                              DataFieldKind::GameplayTag, "tag", "Tag", DataFieldKind::Entity));
    return scope.Commit();
}

std::vector<AuthoredQueryBindingToken> BindPerceptionQueries(AuthoredQueryDispatcher& dispatcher,
                                                             const World& world)
{
    const AuthoredQueryRegistry& registry = dispatcher.Registry();
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&Sees>(registry.Find(kSeesQuery), world));
    tokens.push_back(dispatcher.Bind<&SeesTagged>(registry.Find(kSeesTaggedQuery), world));
    tokens.push_back(dispatcher.Bind<&NearestSeenTagged>(registry.Find(kNearestSeenTaggedQuery), world));
    return tokens;
}
