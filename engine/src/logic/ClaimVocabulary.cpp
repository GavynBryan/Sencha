#include <logic/ClaimVocabulary.h>

#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Claims.h>
#include <world/SimulationAuthority.h>

#include <string>

namespace
{
    DataFieldSchema Field(DataFieldKind kind, std::string key, std::string name, std::string summary,
                          bool required = true)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(name), std::move(summary));
        field.Required = required;
        return field;
    }

    DataFieldSchema GroupAndClaim(DataFieldSchema arguments)
    {
        arguments.Children.push_back(Field(DataFieldKind::GameplayTag, "group", "Group",
                                           "The tag the group's members carry."));
        arguments.Children.push_back(Field(DataFieldKind::GameplayTag, "claim", "Claim", "Which claim."));
        return arguments;
    }

    AuthoredQueryStatus CountOf(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        GameplayTagId group;
        GameplayTagId claim;
        const ClaimBoard* board = world.TryGetResource<ClaimBoard>();
        if (!arguments[0].TryGetTag(group) || !arguments[1].TryGetTag(claim))
            return AuthoredQueryStatus::InvalidArguments;
        if (board == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Int(board->Count(world, group, claim));
        return AuthoredQueryStatus::Value;
    }

    AuthoredQueryStatus Held(const World& world, std::span<const AuthoredValue> arguments, AuthoredValue& result)
    {
        EntityId holder;
        GameplayTagId group;
        GameplayTagId claim;
        const ClaimBoard* board = world.TryGetResource<ClaimBoard>();
        if (!arguments[0].TryGetEntity(holder) || !arguments[1].TryGetTag(group) || !arguments[2].TryGetTag(claim))
            return AuthoredQueryStatus::InvalidArguments;
        if (board == nullptr)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Bool(board->Holds(world, group, claim, holder));
        return AuthoredQueryStatus::Value;
    }
}

VerbAdmission HoldClaim(World& world, const VerbInvocation& invocation)
{
    ClaimBoard* board = world.TryGetResource<ClaimBoard>();
    if (board == nullptr)
        return VerbAdmission::Unavailable;
    if (invocation.Step == LeaseStep::Release)
    {
        board->ReleaseAll(invocation.Owner);
        return VerbAdmission::Accepted;
    }
    GameplayTagId group;
    GameplayTagId claim;
    std::int64_t count = 1;
    double share = 0.0;
    if (!invocation.Owner.IsSet() || invocation.Arguments == nullptr
        || !invocation.Arguments->TryGetTag(0, group) || !invocation.Arguments->TryGetTag(1, claim)
        || !invocation.Arguments->TryGetInt(2, count) || !invocation.Arguments->TryGetFloat(3, share)
        || count < 0 || !(share >= 0.0))
        return VerbAdmission::InvalidArguments;
    if (!IsSimulationAuthority(world))
        return VerbAdmission::Refused;
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const std::uint32_t members = tags != nullptr ? CountGroupMembers(world, group, *tags) : 0;
    const std::uint32_t capacity =
        ClaimCapacity(static_cast<std::uint32_t>(count), static_cast<float>(share), members);
    return board->Take(world, group, claim, invocation.Owner, capacity) ? VerbAdmission::Accepted
                                                                        : VerbAdmission::Refused;
}

bool InstallClaimVocabulary(World& world)
{
    if (!world.HasResource<ClaimBoard>())
        world.AddResource<ClaimBoard>();
    AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(world);
    VerbRegistry* verbs = FindVerbRegistry(world);
    if (queries == nullptr || verbs == nullptr)
        return false;
    if (queries->Find(kClaimsCountQuery).IsValid())
        return true;

    AuthoredQueryRegistrationScope queryScope(*queries, "engine");
    AuthoredQueryDefinition count;
    count.Name = std::string(kClaimsCountQuery);
    count.DisplayName = "Claim count";
    count.Description = "How many places in a group's claim are held.";
    count.Category = "Claims";
    count.Arguments = GroupAndClaim(std::move(count.Arguments));
    count.Result.Kind = DataFieldKind::Int;
    (void)queryScope.Declare(std::move(count));
    AuthoredQueryDefinition held;
    held.Name = std::string(kClaimsHeldQuery);
    held.DisplayName = "Holds claim";
    held.Description = "Whether the entity holds a place in a group's claim.";
    held.Category = "Claims";
    held.Arguments.Children.push_back(Field(DataFieldKind::Entity, "holder", "Holder", "Who might hold it."));
    held.Arguments = GroupAndClaim(std::move(held.Arguments));
    held.Result.Kind = DataFieldKind::Bool;
    (void)queryScope.Declare(std::move(held));
    if (!queryScope.Commit())
        return false;

    VerbRegistrationScope verbScope(*verbs, "engine");
    VerbDefinition hold;
    hold.Name = std::string(kClaimsHoldLease);
    hold.DisplayName = "Hold claim";
    hold.Description = "Holds a place in a group's claim while held: a role, a token, a signal. Refused when every "
                       "place is taken.";
    hold.Category = "Claims";
    hold.Kind = VerbKind::Lease;
    hold.Arguments = GroupAndClaim(std::move(hold.Arguments));
    DataFieldSchema places = Field(DataFieldKind::Int, "count", "Count", "Places the claim holds.", false);
    places.Default = std::int64_t{ 1 };
    hold.Arguments.Children.push_back(std::move(places));
    DataFieldSchema share = Field(DataFieldKind::Float, "share", "Share",
                                  "Places as a share of the group's members, when that is more.", false);
    share.Default = 0.0;
    hold.Arguments.Children.push_back(std::move(share));
    (void)verbScope.Declare(std::move(hold));
    return verbScope.Commit();
}

VerbBindingToken BindClaimVerbs(VerbDispatcher& dispatcher, World& world)
{
    return dispatcher.Bind<&HoldClaim>(dispatcher.Registry().Find(kClaimsHoldLease), world);
}

std::vector<AuthoredQueryBindingToken> BindClaimQueries(AuthoredQueryDispatcher& dispatcher, const World& world)
{
    std::vector<AuthoredQueryBindingToken> tokens;
    tokens.push_back(dispatcher.Bind<&CountOf>(dispatcher.Registry().Find(kClaimsCountQuery), world));
    tokens.push_back(dispatcher.Bind<&Held>(dispatcher.Registry().Find(kClaimsHeldQuery), world));
    return tokens;
}
