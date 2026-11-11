#include <logic/decision/DecisionAgentSystem.h>

#include <abilities/AbilityActivations.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <core/logging/Logger.h>
#include <debug/DebugLines.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Claims.h>
#include <perception/ObservationSharing.h>
#include <perception/PerceptionVocabulary.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace
{
    constexpr std::uint32_t kStaggerSlots = 8;
    // Within this of a step on the ground plane, the step counts as reached.
    constexpr float kArrivedMetres = 1.0f;
    // Within this of a visited place on the ground plane, it counts as reached.
    constexpr float kVisitedMetres = 2.5f;
    constexpr std::size_t kVisitedPlaces = 32;
    // The holder key an agent's one claim is held under.
    constexpr std::uint64_t kClaimKey = 1;

    const Vec4 kDestinationColor{ 0.2f, 0.9f, 0.4f, 1.0f };
    const Vec4 kTargetColor{ 0.95f, 0.3f, 0.25f, 1.0f };
    const Vec4 kCandidateColor{ 0.35f, 0.6f, 0.95f, 1.0f };
    const Vec4 kRejectedColor{ 0.4f, 0.4f, 0.4f, 1.0f };

    enum class EntityRole : std::uint8_t
    {
        Self,
        Target,
        None,
    };

    struct OptionRuntime
    {
        const DecisionOptionPlan* Plan = nullptr;
        AbilityId Ability;
        GameplayTagId ClaimTag;
    };

    struct ProfileRuntime
    {
        const DecisionProfile* Profile = nullptr;
        std::optional<DataAssetLease<SelectionTableDesc>> TableLease;
        SelectionTableBinding Table;
        bool TableBound = false;
        bool Reported = false;
        GameplayTagId Hostile;
        std::vector<const DecisionInput*> Inputs;
        std::vector<EntityRole> Entities;
        std::vector<OptionRuntime> Options;
    };

    // Places a group has visited and when, oldest overwritten first.
    struct VisitLog
    {
        GameplayTagId Group;
        std::vector<Vec3d> Places;
        std::vector<double> When;
        std::size_t Next = 0;
    };

    struct Due
    {
        EntityId Entity;
        DecisionAgent* Agent;
        const ObservationMemory* Memory;
        Vec3d Position;
    };

    [[nodiscard]] std::uint64_t Bit(std::uint32_t option) { return option < 64 ? std::uint64_t{ 1 } << option : 0; }

    [[nodiscard]] float FlatDistance(const Vec3d& a, const Vec3d& b)
    {
        Vec3d between = a - b;
        between.Y = 0.0f;
        return between.Magnitude();
    }

    void Request(World& world, AbilityRequestKind kind, EntityId actor, AbilityId ability)
    {
        if (AbilityRequestQueue* queue = world.TryGetResource<AbilityRequestQueue>())
            queue->Push({ .Kind = kind, .Actor = actor, .Ability = ability, .Instigator = actor });
    }

    // What memory says of the target, kept for when memory lets go.
    void Know(DecisionAgent& agent, const ObservationMemoryEntry& target, const World& world, EntityId self)
    {
        agent.Engaged = true;
        agent.ContactPosition = target.Position;
        agent.ContactVelocity = target.Velocity;
        agent.ContactFacing = target.Facing;
        agent.SecondsSinceContact = target.Age;
        if (Sees(world, self, target.Subject))
            agent.SecondsSinceSeen = 0.0f;
    }

    void Tick(DecisionAgent& agent, float elapsed)
    {
        agent.OptionSeconds += elapsed;
        agent.DestinationSeconds += elapsed;
        agent.SecondsSinceContact += elapsed;
        agent.SecondsSinceSeen += elapsed;
        if (agent.Unavailable != 0 && (agent.UnavailableSeconds -= elapsed) <= 0.0f)
            agent.Unavailable = 0;
        agent.UntilDecision -= elapsed;
    }

    std::uint64_t SelectionSeed(EntityId self, const DecisionAgent& agent)
    {
        return (static_cast<std::uint64_t>(self.Index) << 32) ^ (agent.Decisions * 0x9E3779B97F4A7C15ull);
    }

    // A chosen place stops being worth anything once the target has moved far
    // from where it was when the place was chosen, once it is old, or, for a
    // step, once the agent stands on it.
    bool PlaceStale(const ProfileRuntime& runtime, const DecisionAgent& agent, const ObservationMemoryEntry* target,
                    const Vec3d& position)
    {
        if (!agent.HasDestination || agent.PlaceOption >= runtime.Options.size()
            || runtime.Options[agent.PlaceOption].Plan == nullptr)
            return true;
        const DecisionOptionPlan& plan = *runtime.Options[agent.PlaceOption].Plan;
        return agent.DestinationSeconds > plan.PositionSeconds
            || (plan.ReplaceOnArrival && FlatDistance(agent.Destination, position) <= kArrivedMetres)
            || (target != nullptr && (target->Position - agent.DestinationTarget).Magnitude() > plan.RepositionDistance);
    }
}

struct DecisionAgents::State
{
    State(Services services, DecisionHooks hooks) : Use(services), Hooks(std::move(hooks)) {}

    Services Use;
    DecisionHooks Hooks;
    std::unordered_map<std::string, DecisionInput> Inputs;
    const World* QueriedWorld = nullptr;
    std::optional<Query<Write<DecisionAgent>, Read<ObservationMemory>, Read<WorldTransform>>> Agents;
    std::vector<std::unique_ptr<ProfileRuntime>> Profiles;
    std::vector<DecisionRecord> Records;
    std::vector<VisitLog> Visits;
    SelectionScratch Selection;
    std::vector<float> InputValues;
    std::vector<EntityId> EntityValues;
    std::vector<std::string> Errors;
    std::vector<Due> DueNow;
    GameplayTagId Group;
    double Now = 0.0;
    std::uint32_t RunsThisTick = 0;
    bool WasDrawing = false;
    std::uint64_t CostTicks = 0;
    double CostSeconds = 0.0;
    double WorstCostSeconds = 0.0;

    DecisionRecord& RecordFor(EntityId agent)
    {
        for (DecisionRecord& record : Records)
            if (record.Agent == agent)
                return record;
        Records.push_back({ .Agent = agent });
        return Records.back();
    }

    [[nodiscard]] const ProfileRuntime* Resolved(const DecisionProfile* profile) const
    {
        const auto found = std::ranges::find(Profiles, profile, [](const auto& runtime) { return runtime->Profile; });
        return found != Profiles.end() ? found->get() : nullptr;
    }

    ProfileRuntime* RuntimeFor(const World& world, const DecisionProfile* profile)
    {
        if (profile == nullptr)
            return nullptr;
        auto found = std::ranges::find(Profiles, profile, [](const auto& runtime) { return runtime->Profile; });
        if (found == Profiles.end())
        {
            auto runtime = std::make_unique<ProfileRuntime>();
            runtime->Profile = profile;
            runtime->TableLease.emplace(profile->Table, kSelectionTableTypeName);
            if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
                runtime->Hostile = tags->FindTag(profile->Hostile);
            Profiles.push_back(std::move(runtime));
            found = Profiles.end() - 1;
        }
        return Bind(world, **found) ? found->get() : nullptr;
    }

    bool Bind(const World& world, ProfileRuntime& runtime)
    {
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        const AuthoredQueryRegistry* queries = Use.Queries != nullptr ? &Use.Queries->Registry() : nullptr;
        Errors.clear();
        bool rebound = false;
        if (!runtime.TableBound)
        {
            if (runtime.TableLease->Get(Use.Assets->Assets, Use.Assets->DataAssets) == nullptr)
                return false;
            runtime.TableBound = runtime.Table.BindFrom(Use.Assets->DataAssets, runtime.TableLease->Handle(), queries,
                                                        tags, Errors);
            rebound = true;
        }
        else
        {
            rebound = runtime.Table.Refresh(Use.Assets->DataAssets, queries, tags, Errors);
        }
        const SelectionTable* table = runtime.Table.Table();
        if (table == nullptr)
        {
            Report(runtime, "its table did not bind");
            return false;
        }
        if (rebound)
            Map(runtime, *table);
        return true;
    }

    void Map(ProfileRuntime& runtime, const SelectionTable& table)
    {
        runtime.Inputs.clear();
        for (const std::string& name : table.Inputs())
        {
            const auto found = Inputs.find(name);
            runtime.Inputs.push_back(found != Inputs.end() ? &found->second : nullptr);
            if (found == Inputs.end() && Use.Log != nullptr)
                Use.Log->Error("Decision table '{}' reads input '{}', which the game does not supply", table.Name(), name);
        }
        runtime.Entities.clear();
        for (const std::string& name : table.Entities())
            runtime.Entities.push_back(name == "querier" ? EntityRole::Self
                                       : name == "target"  ? EntityRole::Target
                                                           : EntityRole::None);
        runtime.Options.clear();
        for (const SelectionOption& option : table.Options())
        {
            OptionRuntime& out = runtime.Options.emplace_back();
            out.Plan = runtime.Profile->PlanFor(option.Name);
            if (out.Plan == nullptr && Use.Log != nullptr)
                Use.Log->Error("Decision table '{}' option '{}' has no plan in its profile", table.Name(), option.Name);
        }
        if (Hooks.Bound)
            Hooks.Bound(*runtime.Profile);
    }

    void Report(ProfileRuntime& runtime, std::string_view what)
    {
        if (runtime.Reported || Use.Log == nullptr)
            return;
        runtime.Reported = true;
        std::string detail;
        for (const std::string& error : Errors)
            detail += "; " + error;
        Use.Log->Error("Decision profile for table '{}': {}{}", runtime.Profile->Table, what, detail);
    }

    const ObservationMemoryEntry* ChooseTarget(const World& world, const ProfileRuntime& runtime,
                                               const ObservationMemory& memory, const Vec3d& position) const
    {
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (tags == nullptr || !runtime.Hostile.IsValid())
            return nullptr;
        const ObservationMemoryEntry* chosen = nullptr;
        float nearest = 0.0f;
        for (std::uint32_t index = 0; index < memory.Count; ++index)
        {
            const ObservationMemoryEntry& entry = memory.Entries[index];
            if (!entry.Subject.IsValid() || !world.IsAlive(entry.Subject))
                continue;
            const GameplayTagContainer* held = world.TryGet<GameplayTagContainer>(entry.Subject);
            if (held == nullptr || !held->HasDescendantOf(*tags, runtime.Hostile))
                continue;
            const float distance = (entry.Position - position).SqrMagnitude();
            if (chosen == nullptr || distance < nearest)
            {
                chosen = &entry;
                nearest = distance;
            }
        }
        return chosen;
    }

    AbilityId AbilityOf(const World& world, OptionRuntime& option) const
    {
        if (!option.Ability.IsValid() && option.Plan != nullptr)
            if (const AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>())
                option.Ability = abilities->Find(option.Plan->Ability);
        return option.Ability;
    }

    GameplayTagId ClaimTagOf(const World& world, OptionRuntime& option) const
    {
        if (!option.ClaimTag.IsValid() && option.Plan != nullptr && !option.Plan->Claim.empty())
            if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
                option.ClaimTag = tags->FindTag(option.Plan->Claim);
        return option.ClaimTag;
    }

    std::uint32_t ClaimCapacityFor(const World& world, const DecisionOptionPlan& plan) const
    {
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        const std::uint32_t members = tags != nullptr ? CountGroupMembers(world, Group, *tags) : 0;
        return ClaimCapacity(plan.ClaimCount, plan.ClaimShare, members);
    }

    void ReleaseClaim(World& world, EntityId self, DecisionAgent& agent)
    {
        if (agent.ClaimOption == kNoDecisionOption)
            return;
        if (ClaimBoard* board = world.TryGetResource<ClaimBoard>())
            board->ReleaseAll(LeaseOwner{ self, kClaimKey });
        agent.ClaimOption = kNoDecisionOption;
    }

    // An option with no plan or ability, or whose claim others have filled, is ruled out.
    std::uint64_t Unavailable(World& world, EntityId self, ProfileRuntime& runtime, const DecisionAgent& agent)
    {
        std::uint64_t unavailable = agent.Unavailable;
        const ClaimBoard* board = world.TryGetResource<ClaimBoard>();
        for (std::uint32_t index = 0; index < runtime.Options.size(); ++index)
        {
            OptionRuntime& candidate = runtime.Options[index];
            if (candidate.Plan == nullptr || !AbilityOf(world, candidate).IsValid())
            {
                unavailable |= Bit(index);
                continue;
            }
            const GameplayTagId claim = ClaimTagOf(world, candidate);
            if (claim.IsValid() && Group.IsValid() && board != nullptr && !board->Holds(world, Group, claim, self)
                && board->Count(world, Group, claim) >= ClaimCapacityFor(world, *candidate.Plan))
                unavailable |= Bit(index);
        }
        return unavailable;
    }

    void Gather(const ProfileRuntime& runtime, const DecisionMoment& moment)
    {
        InputValues.clear();
        for (const DecisionInput* read : runtime.Inputs)
            InputValues.push_back(read != nullptr ? (*read)(moment) : 0.0f);
        EntityValues.clear();
        for (const EntityRole role : runtime.Entities)
            EntityValues.push_back(role == EntityRole::Self     ? moment.Self
                                   : role == EntityRole::Target ? moment.Agent.Target
                                                                : EntityId{});
    }

    void Stop(World& world, EntityId self, DecisionAgent& agent)
    {
        if (Hooks.Stopped)
            Hooks.Stopped(world, self);
        if (agent.Running.IsValid())
            Request(world, AbilityRequestKind::Cancel, self, agent.Running);
        agent.Running = {};
        agent.Option = kNoDecisionOption;
        agent.HasDestination = false;
    }

    void Track(World& world, EntityId self, DecisionAgent& agent, const ProfileRuntime& runtime,
               const ObservationMemoryEntry* target, const Vec3d& position)
    {
        if (target != nullptr && !agent.Engaged && Hooks.TargetFound)
            Hooks.TargetFound(world, self);
        agent.Target = target != nullptr ? target->Subject : EntityId{};
        if (target != nullptr)
            Know(agent, *target, world, self);
        Group = GroupOf(world, self);
        if (Hooks.Prepare)
            Hooks.Prepare(world, self, agent, target, position);
        const SelectionTable& table = *runtime.Table.Table();
        if (agent.Option < runtime.Options.size() && agent.Running.IsValid() && !IsAbilityLive(world, self, agent.Running))
        {
            if (Hooks.OptionEnded)
                Hooks.OptionEnded(world, self, table.Options()[agent.Option].Name);
            agent.Option = kNoDecisionOption;
            agent.Running = {};
        }
        if (agent.PlaceOption != kNoDecisionOption && PlaceStale(runtime, agent, target, position))
            agent.PositionQuality = 0.0f;
    }

    // False when every place the claim holds is taken.
    bool TakeClaim(World& world, EntityId self, DecisionAgent& agent, OptionRuntime& option, std::uint32_t index)
    {
        if (agent.ClaimOption == index)
            return true;
        ReleaseClaim(world, self, agent);
        const GameplayTagId claim = ClaimTagOf(world, option);
        ClaimBoard* claims = world.TryGetResource<ClaimBoard>();
        if (!claim.IsValid() || !Group.IsValid() || claims == nullptr)
            return true;
        if (!claims->Take(world, Group, claim, LeaseOwner{ self, kClaimKey }, ClaimCapacityFor(world, *option.Plan)))
            return false;
        agent.ClaimOption = index;
        return true;
    }

    void RecordVisited(const Vec3d& place)
    {
        if (!Group.IsValid())
            return;
        auto found = std::ranges::find(Visits, Group, &VisitLog::Group);
        if (found == Visits.end())
            found = Visits.insert(Visits.end(), VisitLog{ .Group = Group, .Places = {}, .When = {}, .Next = 0 });
        if (found->Places.size() < kVisitedPlaces)
        {
            found->Places.push_back(place);
            found->When.push_back(Now);
        }
        else
        {
            found->Places[found->Next] = place;
            found->When[found->Next] = Now;
        }
        found->Next = (found->Next + 1) % kVisitedPlaces;
    }

    // False when the option must wait: for the tick's budget, or for somewhere to go.
    bool Place(World& world, EntityId self, DecisionAgent& agent, const ProfileRuntime& runtime, std::uint32_t index,
               const ObservationMemoryEntry* target, const Vec3d& position, std::uint64_t tick, std::uint32_t budget,
               DecisionRecord& record)
    {
        const SelectionTable& table = *runtime.Table.Table();
        const DecisionOptionPlan& plan = *runtime.Options[index].Plan;
        if (RunsThisTick >= budget)
        {
            agent.UntilDecision = 0.0f;
            record.Note = "waiting for a position run";
            return false;
        }
        if (agent.HasDestination && agent.PlaceOption < runtime.Options.size()
            && runtime.Options[agent.PlaceOption].Plan != nullptr && runtime.Options[agent.PlaceOption].Plan->MarksVisited
            && FlatDistance(agent.Destination, position) <= kVisitedMetres)
            RecordVisited(agent.Destination);
        const DecisionHooks::PlaceChoice choice = Hooks.ChoosePlace
            ? Hooks.ChoosePlace(world, self, agent, plan, target, tick, record)
            : DecisionHooks::PlaceChoice{};
        RunsThisTick += choice.Ran ? 1u : 0u;
        if (!choice.Place)
        {
            record.Nowhere = record.Places;
            agent.Unavailable |= Bit(index);
            agent.UnavailableSeconds = runtime.Profile->UnavailableSeconds;
            agent.UntilDecision = 0.0f;
            record.Note = std::format("{} found nowhere to go", table.Options()[index].Name);
            return false;
        }
        agent.HasDestination = true;
        agent.Destination = choice.Place->Position;
        agent.PositionQuality = choice.Place->Score;
        agent.PlaceOption = index;
        agent.DestinationTarget = target != nullptr ? target->Position : agent.ContactPosition;
        agent.DestinationSeconds = 0.0f;
        return true;
    }

    void GoToLastSeen(DecisionAgent& agent, const ObservationMemoryEntry* target)
    {
        agent.HasDestination = target != nullptr || agent.Engaged;
        agent.Destination = target != nullptr ? target->Position : agent.ContactPosition;
        agent.PositionQuality = 0.0f;
        agent.PlaceOption = kNoDecisionOption;
        agent.DestinationTarget = agent.Destination;
        agent.DestinationSeconds = 0.0f;
    }

    void Execute(World& world, EntityId self, DecisionAgent& agent, const OptionRuntime& option, std::uint32_t index,
                 std::string_view name, bool changed, DecisionRecord& record)
    {
        if (agent.Running.IsValid())
            Request(world, AbilityRequestKind::Cancel, self, agent.Running);
        if (Hooks.OptionChosen)
            Hooks.OptionChosen(world, self, name, changed);
        Request(world, AbilityRequestKind::Activate, self, option.Ability);
        agent.Running = option.Ability;
        if (changed)
            agent.OptionSeconds = 0.0f;
        agent.Option = index;
        record.Note = std::format("{} {}", changed ? "chose" : "moved", name);
    }

    void Act(World& world, EntityId self, DecisionAgent& agent, ProfileRuntime& runtime, std::uint32_t index,
             const ObservationMemoryEntry* target, const Vec3d& position, std::uint64_t tick, std::uint32_t budget,
             DecisionRecord& record)
    {
        const std::string& name = runtime.Table.Table()->Options()[index].Name;
        OptionRuntime& option = runtime.Options[index];
        const DecisionOptionPlan& plan = *option.Plan;
        const bool changed = index != agent.Option;
        if (!changed && (plan.Position.empty() || !PlaceStale(runtime, agent, target, position)))
        {
            record.Note = "holding " + name;
            return;
        }
        if (changed && !TakeClaim(world, self, agent, option, index))
        {
            agent.UntilDecision = 0.0f;
            record.Note = std::format("{} is fully claimed", name);
            return;
        }
        if (!plan.Position.empty())
        {
            if (!Place(world, self, agent, runtime, index, target, position, tick, budget, record))
                return;
        }
        else if (plan.ToLastSeen)
            GoToLastSeen(agent, target);
        Execute(world, self, agent, option, index, name, changed, record);
    }

    void Decide(World& world, const Due& due, std::uint64_t tick, std::uint32_t budget)
    {
        const EntityId self = due.Entity;
        DecisionAgent& agent = *due.Agent;
        ProfileRuntime* runtime = RuntimeFor(world, Hooks.ProfileOf ? Hooks.ProfileOf(world, self) : nullptr);
        if (runtime == nullptr)
            return;
        const SelectionTable& table = *runtime->Table.Table();
        DecisionRecord& record = RecordFor(self);
        record.Table = std::string(table.Name());
        ++agent.Decisions;

        const ObservationMemoryEntry* target = ChooseTarget(world, *runtime, *due.Memory, due.Position);
        Track(world, self, agent, *runtime, target, due.Position);
        Gather(*runtime, DecisionMoment{ self, agent, due.Position, target });
        const std::uint64_t unavailable = Unavailable(world, self, *runtime, agent);
        const std::optional<std::uint32_t> current =
            agent.Option < runtime->Options.size() ? std::optional(agent.Option) : std::nullopt;
        const SelectionEvaluator evaluator(world, Use.Queries);
        const SelectionResult result = evaluator.Evaluate(
            table,
            SelectionContext{ .Entities = EntityValues, .Inputs = InputValues, .Current = current,
                              .CurrentSeconds = agent.OptionSeconds, .Unavailable = unavailable,
                              .Seed = SelectionSeed(self, agent) },
            Selection, &record.Trace);
        if (result.Status != SelectionStatus::Success)
        {
            ReleaseClaim(world, self, agent);
            Stop(world, self, agent);
            record.Note = result.Status == SelectionStatus::NoneQualified ? "no option qualifies" : "context missing";
            return;
        }
        Act(world, self, agent, *runtime, result.Option, target, due.Position, tick, budget, record);
    }

    // Its clocks run every tick; whether it decides is the game's say, the
    // stagger's, and whether the option it holds lets go.
    bool Advance(World& world, EntityId entity, DecisionAgent& agent, const ObservationMemory& memory, float elapsed,
                 bool frozen)
    {
        if (!agent.Started)
        {
            agent.Started = true;
            agent.UntilDecision = static_cast<float>(entity.Index % kStaggerSlots) / kStaggerSlots * 0.25f;
        }
        const DecisionTurn turn = Hooks.Turn ? Hooks.Turn(world, entity, elapsed) : DecisionTurn::Decide;
        if (turn == DecisionTurn::Skip)
            return false;
        Tick(agent, elapsed);
        if (const ObservationMemoryEntry* known = FindRemembered(memory, agent.Target))
            Know(agent, *known, world, entity);
        const ProfileRuntime* resolved = Hooks.ProfileOf ? Resolved(Hooks.ProfileOf(world, entity)) : nullptr;
        const DecisionOptionPlan* underway =
            resolved != nullptr && agent.Option < resolved->Options.size() ? resolved->Options[agent.Option].Plan : nullptr;
        const bool committed = underway != nullptr && underway->Commits && IsAbilityLive(world, entity, agent.Running);
        return !frozen && turn == DecisionTurn::Decide && !committed && agent.UntilDecision <= 0.0f;
    }

    void Draw(World& world, const DecisionRunSettings& settings)
    {
        if (!world.HasResource<DebugLineChannels>())
            world.AddResource<DebugLineChannels>();
        DebugLineChannels& channels = world.GetResource<DebugLineChannels>();
        DebugLineList& lines = channels.Lines(channels.Channel(settings.DebugChannel));
        lines.Clear();
        if (!settings.Drawing)
            return;
        std::as_const(world).ForEachComponent<DecisionAgent>([&](EntityId entity, const DecisionAgent& agent) {
            const WorldTransform* pose = world.TryGet<WorldTransform>(entity);
            if (pose != nullptr && agent.HasDestination)
            {
                lines.AddOnTop(pose->Value.Position, agent.Destination, kDestinationColor);
                AppendMarker(lines, agent.Destination, 0.4f, kDestinationColor);
            }
            const ObservationMemory* memory = world.TryGet<ObservationMemory>(entity);
            if (const ObservationMemoryEntry* target = memory != nullptr ? FindRemembered(*memory, agent.Target) : nullptr)
            {
                AppendMarker(lines, target->Position, 0.3f, kTargetColor);
                lines.AddOnTop(target->Position, target->Position + target->Velocity * 0.5f, kTargetColor);
            }
        });
        for (const DecisionRecord& record : Records)
        {
            if (!record.Places)
                continue;
            for (const CandidateTraceRow& row : record.Places->Rows())
            {
                const bool rejected = row.Outcome == CandidateTraceOutcome::Rejected;
                const Vec3d top = row.Position + Vec3d(0.0f, rejected ? 0.2f : 0.2f + row.Score, 0.0f);
                lines.AddTested(row.Position, top, rejected ? kRejectedColor : kCandidateColor);
            }
        }
    }
};

DecisionAgents::DecisionAgents(Services services, DecisionHooks hooks)
    : Impl(std::make_unique<State>(services, std::move(hooks)))
{
}

DecisionAgents::~DecisionAgents() = default;

void DecisionAgents::RegisterInput(std::string name, DecisionInput input)
{
    Impl->Inputs.insert_or_assign(std::move(name), std::move(input));
}

void DecisionAgents::Close()
{
    Impl->Profiles.clear();
    Impl->Records.clear();
}

void DecisionAgents::Run(FixedLogicContext& ctx, const DecisionRunSettings& settings)
{
    World& world = ctx.Entities;
    State& state = *Impl;
    if (!world.IsRegistered<DecisionAgent>() || !world.IsRegistered<ObservationMemory>() || !IsSimulationAuthority(world)
        || state.Use.Assets == nullptr)
        return;
    if (state.QueriedWorld != &world)
    {
        state.Agents.emplace(world);
        state.QueriedWorld = &world;
    }
    const auto started = std::chrono::steady_clock::now();
    const float elapsed = static_cast<float>(ctx.Time.DeltaSeconds);
    state.Now += elapsed;
    state.RunsThisTick = 0;

    state.DueNow.clear();
    state.Agents->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        auto agents = view.template Write<DecisionAgent>();
        const auto memories = view.template Read<ObservationMemory>();
        const auto transforms = view.template Read<WorldTransform>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            if (state.Advance(world, view.Entity(row), agents[row], memories[row], elapsed, settings.Frozen))
                state.DueNow.push_back({ view.Entity(row), &agents[row], &memories[row], transforms[row].Value.Position });
    });
    std::ranges::sort(state.DueNow, {}, &Due::Entity);
    for (const Due& due : state.DueNow)
    {
        if (const DecisionProfile* profile = state.Hooks.ProfileOf ? state.Hooks.ProfileOf(world, due.Entity) : nullptr)
            due.Agent->UntilDecision = std::max(due.Agent->UntilDecision + profile->DecisionSeconds, 0.0f);
        state.Decide(world, due, ctx.Time.TickIndex, settings.PlaceRuns);
    }
    std::erase_if(state.Records, [&](const DecisionRecord& record) { return !world.IsAlive(record.Agent); });

    if (settings.Drawing || state.WasDrawing)
        state.Draw(world, settings);
    state.WasDrawing = settings.Drawing;

    const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    ++state.CostTicks;
    state.CostSeconds += cost;
    state.WorstCostSeconds = std::max(state.WorstCostSeconds, cost);
}

bool DecisionAgents::HasBound(const DecisionProfile& profile) const
{
    return Impl->Resolved(&profile) != nullptr;
}

std::string_view DecisionAgents::OptionName(const World& world, const DecisionProfile& profile, std::uint32_t option)
{
    const ProfileRuntime* runtime = Impl->RuntimeFor(world, &profile);
    const SelectionTable* table = runtime != nullptr ? runtime->Table.Table() : nullptr;
    return table != nullptr && option < table->Options().size() ? std::string_view(table->Options()[option].Name)
                                                                : std::string_view{};
}

GameplayTagId DecisionAgents::GroupOf(const World& world, EntityId agent)
{
    const ObservationSharing* sharing =
        world.IsRegistered<ObservationSharing>() ? world.TryGet<ObservationSharing>(agent) : nullptr;
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    return sharing != nullptr && tags != nullptr ? tags->FindTag(sharing->Group.View()) : GameplayTagId{};
}

void DecisionAgents::CollectVisited(GameplayTagId group, float seconds, std::vector<Vec3d>& out) const
{
    out.clear();
    const auto found = std::ranges::find(Impl->Visits, group, &VisitLog::Group);
    if (!group.IsValid() || found == Impl->Visits.end())
        return;
    for (std::size_t index = 0; index < found->Places.size(); ++index)
        if (Impl->Now - found->When[index] < seconds)
            out.push_back(found->Places[index]);
}

std::string DecisionAgents::Describe(EntityId agent) const
{
    const auto found = std::ranges::find(Impl->Records, agent, &DecisionRecord::Agent);
    if (found == Impl->Records.end())
        return "no decision yet";
    std::string text = std::format("table '{}': {}\n", found->Table, found->Note);
    text += DescribeSelectionTrace(found->Trace);
    if (found->Places)
        text += DescribeCandidateTrace(*found->Places);
    if (found->Nowhere)
        text += "  last run that found nowhere:\n" + DescribeCandidateTrace(*found->Nowhere);
    return text;
}

std::string DecisionAgents::DescribeCost() const
{
    const State& state = *Impl;
    const double mean = state.CostTicks > 0 ? state.CostSeconds / static_cast<double>(state.CostTicks) : 0.0;
    return std::format("decision cost: mean {:.4f} ms, worst {:.4f} ms a tick over {} ticks", mean * 1000.0,
                       state.WorstCostSeconds * 1000.0, state.CostTicks);
}
