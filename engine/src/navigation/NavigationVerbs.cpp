#include <navigation/NavigationVerbs.h>

#include <authored/AuthoredValue.h>
#include <ecs/World.h>
#include <navigation/NavAgentRequests.h>

#include <string>
#include <utility>

namespace
{
    DataFieldSchema AgentTargetField()
    {
        DataFieldSchema agent = MakeDataField(DataFieldKind::Entity, "target", "Agent", "The agent given the order.");
        agent.Reference.ComponentIdentity = std::string(ComponentDefinition<NavAgent>::Identity);
        return agent;
    }

    DataFieldSchema OptionalNumberField(std::string key, std::string label, std::string summary, double fallback)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(label), std::move(summary));
        field.Required = false;
        field.Default = fallback;
        field.Numeric.Minimum = 0.0;
        return field;
    }

    VerbDefinition OrderVerbDefinition(std::string_view name, std::string display, std::string description)
    {
        VerbDefinition verb;
        verb.Name = std::string(name);
        verb.DisplayName = std::move(display);
        verb.Description = std::move(description);
        verb.Category = "Navigation";
        verb.Arguments.Children.push_back(AgentTargetField());
        return verb;
    }

    template <typename E>
    DataFieldSchema EnumResultField(std::string key)
    {
        DataFieldSchema result = MakeDataField(DataFieldKind::Enum, std::move(key), {});
        for (const auto& value : EnumSchema<E>::Values)
            result.EnumChoices.push_back({ std::string(value.Name), std::string(value.Display), {} });
        return result;
    }

    AuthoredQueryDefinition AgentQuery(std::string_view name, std::string display, std::string description,
                                       DataFieldSchema result)
    {
        AuthoredQueryDefinition query;
        query.Name = std::string(name);
        query.DisplayName = std::move(display);
        query.Description = std::move(description);
        query.Category = "Navigation";
        query.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "agent", "Agent"));
        query.Result = std::move(result);
        return query;
    }

    const NavAgentProgress* QueriedAgentProgress(const World& world, std::span<const AuthoredValue> arguments)
    {
        EntityId agent;
        if (arguments.empty() || !arguments[0].TryGetEntity(agent) || !world.IsRegistered<NavAgentProgress>())
            return nullptr;
        return world.TryGet<NavAgentProgress>(agent);
    }

    AuthoredQueryStatus EvaluatePhaseQuery(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        const NavAgentProgress* progress = QueriedAgentProgress(world, arguments);
        if (progress == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Enum(std::string(NavEnumName(progress->Phase)));
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus EvaluateFailureQuery(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        const NavAgentProgress* progress = QueriedAgentProgress(world, arguments);
        if (progress == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Enum(std::string(NavEnumName(progress->Failure)));
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus EvaluateRemainingQuery(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        const NavAgentProgress* progress = QueriedAgentProgress(world, arguments);
        if (progress == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Float(progress->Remaining);
        return AuthoredQueryStatus::Value;
    }

    float OptionalFloatArgument(const VerbInvocation& invocation, std::size_t slot, double fallback)
    {
        double value = fallback;
        if (!invocation.Arguments->TryGetFloat(slot, value))
            value = fallback;
        return static_cast<float>(value);
    }

    VerbAdmission AdmissionFor(NavRequestId issued)
    {
        return issued.IsValid() ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
}

void DeclareNavigationVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition moveTo = OrderVerbDefinition(kNavigationMoveToVerb, "Move to", "Sends the agent to a point, replacing any order it has.");
    DataFieldSchema point = MakeDataField(DataFieldKind::Vector, "point", "Point", "Where to go.");
    point.VectorLength = 3;
    moveTo.Arguments.Children.push_back(std::move(point));
    moveTo.Arguments.Children.push_back(
        OptionalNumberField("arrive_radius", "Arrive radius", "Metres from the point that count as there; zero is the default.", 0.0));
    moveTo.Arguments.Children.push_back(OptionalNumberField("speed", "Speed", "The fraction of its top speed it walks at.", 1.0));
    (void)scope.Declare(std::move(moveTo));

    VerbDefinition approach =
        OrderVerbDefinition(kNavigationApproachVerb, "Approach", "Sends the agent to an entity, replacing any order it has.");
    approach.Arguments.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "subject", "Subject", "What to go to; the order fails if it goes away."));
    DataFieldSchema follow = MakeDataField(DataFieldKind::Bool, "follow", "Follow",
                                           "Keep up with it after arriving instead of ending the order.");
    follow.Required = false;
    follow.Default = false;
    approach.Arguments.Children.push_back(std::move(follow));
    approach.Arguments.Children.push_back(
        OptionalNumberField("arrive_radius", "Arrive radius", "Metres from it that count as there; zero is the default.", 0.0));
    approach.Arguments.Children.push_back(OptionalNumberField("speed", "Speed", "The fraction of its top speed it walks at.", 1.0));
    (void)scope.Declare(std::move(approach));

    (void)scope.Declare(OrderVerbDefinition(kNavigationStopVerb, "Stop", "Ends the agent's order; it stands where it is."));
}

void DeclareNavigationQueries(AuthoredQueryRegistrationScope& scope)
{
    (void)scope.Declare(AgentQuery(kNavigationPhaseQuery, "Navigation phase",
                                   "Where the agent's latest order stands.", EnumResultField<NavAgentPhase>("phase")));
    (void)scope.Declare(AgentQuery(kNavigationFailureQuery, "Navigation failure",
                                   "Why the agent's latest order failed; none while it has not.",
                                   EnumResultField<NavAgentFailure>("failure")));
    (void)scope.Declare(AgentQuery(kNavigationRemainingQuery, "Distance remaining",
                                   "Metres of route the agent still has to walk.",
                                   MakeDataField(DataFieldKind::Float, "remaining", "Remaining")));
}

VerbAdmission NavigationOperations::MoveTo(const VerbInvocation& invocation)
{
    EntityId agent;
    AuthoredVectorValue point;
    NavDestination destination;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, agent)
        || !invocation.Arguments->TryGetVector(1, point))
        return VerbAdmission::InvalidArguments;
    destination.Point = Vec3d(static_cast<float>(point.Components[0]), static_cast<float>(point.Components[1]),
                              static_cast<float>(point.Components[2]));
    destination.ArriveRadius = OptionalFloatArgument(invocation, 2, 0.0);
    destination.SpeedScale = OptionalFloatArgument(invocation, 3, 1.0);
    return AdmissionFor(RequestNavigation(*Entities, agent, destination));
}

VerbAdmission NavigationOperations::Approach(const VerbInvocation& invocation)
{
    EntityId agent;
    NavDestination destination = NavDestination::AtEntity(EntityId{});
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, agent)
        || !invocation.Arguments->TryGetEntity(1, destination.Target) || !Entities->IsAlive(destination.Target))
        return VerbAdmission::InvalidArguments;
    bool follow = false;
    if (invocation.Arguments->TryGetBool(2, follow))
        destination.Follow = follow;
    destination.ArriveRadius = OptionalFloatArgument(invocation, 3, 0.0);
    destination.SpeedScale = OptionalFloatArgument(invocation, 4, 1.0);
    return AdmissionFor(RequestNavigation(*Entities, agent, destination));
}

VerbAdmission NavigationOperations::Stop(const VerbInvocation& invocation)
{
    EntityId agent;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetEntity(0, agent))
        return VerbAdmission::InvalidArguments;
    return AdmissionFor(CancelNavigation(*Entities, agent));
}

std::vector<AuthoredQueryBindingToken> BindNavigationQueries(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    const AuthoredQueryRegistry& registry = dispatcher.Registry();
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&EvaluatePhaseQuery>(registry.Find(kNavigationPhaseQuery), world));
    tokens.push_back(dispatcher.Bind<&EvaluateFailureQuery>(registry.Find(kNavigationFailureQuery), world));
    tokens.push_back(dispatcher.Bind<&EvaluateRemainingQuery>(registry.Find(kNavigationRemainingQuery), world));
    return tokens;
}
