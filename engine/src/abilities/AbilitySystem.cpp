#include <abilities/AbilitySystem.h>

#include <abilities/AbilityPredictionJournal.h>
#include <world/SimulationTimeline.h>

#include "AbilityPass.h"

#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <app/GameContexts.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionVocabulary.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace
{
    // Buffers a pass reuses, so a warm pass allocates nothing for them.
    struct AbilityPassScratch
    {
        explicit AbilityPassScratch(const World& world)
            : Granted(world)
        {
        }

        struct EffectGrant
        {
            EntityId Target;
            EntityId Effect;
            std::uint16_t Entry = kAbilityNoGrantEntry;
        };

        Query<Read<AbilityGrants>> Granted;
        std::optional<Query<Read<ActiveEffect>>> Effects;
        std::vector<EffectGrant> EffectGrants;
        std::vector<EntityId> Actors;
        std::vector<AbilityRequest> Requests;
        std::vector<std::string> Problems;
        AbilityPassBuffers Buffers;
    };

    const AbilityEffectiveBinding* RecordBinding(const AbilityGrantEntries& entries, const AbilityActor& actor,
                                                 const AbilityRecord& record)
    {
        const AbilityGrants* grants = actor.Grants();
        const AbilityGrant* grant = grants != nullptr ? grants->Find(record.Grant) : nullptr;
        return grant != nullptr ? entries.Binding(grant->Entry) : nullptr;
    }

    // A press: a toggle it ends, else the bindings it starts (docs/gameplay/abilitykit.md,
    // "Grants and input").
    void Press(AbilityPass& pass, const AbilityActor& actor, const AbilityGrantEntries& entries,
               const GameplayTagRegistry* tags, std::uint8_t action, std::uint8_t edge)
    {
        bool toggled = false;
        for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
        {
            AbilityRecord& record = actor.Record(slot);
            const AbilityEffectiveBinding* binding = RecordBinding(entries, actor, record);
            if (record.State != AbilityRecordState::Live || record.TriggerAction != action || binding == nullptr
                || binding->Policy != AbilityInputPolicy::Toggle)
                continue;
            record.Released = true;
            pass.Run(actor, slot);
            toggled = true;
        }
        if (toggled)
            return;

        const AbilityGrants* grants = actor.Grants();
        if (grants == nullptr)
            return;
        static const GameplayTagContainer kNoTags{};
        const GameplayTagContainer* held = actor.Tags();

        // Copies: a start may revoke or add grants, moving the entries they came from.
        std::array<AbilityGrant, kAbilityGrantCapacity> grant{};
        std::array<const AbilityEffectiveBinding*, kAbilityGrantCapacity> binding{};
        std::size_t count = 0;
        for (std::uint8_t i = 0; i < grants->Count; ++i)
        {
            const AbilityEffectiveBinding* bound = entries.Binding(grants->Entries[i].Entry);
            if (bound == nullptr || bound->Action != action)
                continue;
            if (bound->HasContext
                && (tags == nullptr || !bound->Context.Matches(held != nullptr ? *held : kNoTags, *tags)))
                continue;
            grant[count] = grants->Entries[i];
            binding[count] = bound;
            ++count;
        }
        // Priority alone orders them; equal priorities keep grant order.
        std::array<std::size_t, kAbilityGrantCapacity> order{};
        for (std::size_t i = 0; i < count; ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count),
                         [&](std::size_t a, std::size_t b) { return binding[a]->Priority > binding[b]->Priority; });

        const auto keyOf = [&](std::size_t i) {
            return AbilityActivationKey{ .CommandTick = pass.Tick(), .Ability = grant[i].Ability, .Origin = edge };
        };
        for (std::size_t at = 0; at < count;)
        {
            const std::size_t i = order[at];
            const std::int32_t priority = binding[i]->Priority;
            std::size_t exclusives = 0;
            std::size_t next = at;
            for (; next < count && binding[order[next]]->Priority == priority; ++next)
                exclusives += binding[order[next]]->Consume == AbilityConsume::Exclusive ? 1 : 0;
            if (exclusives > 1)
            {
                pass.Trace({ .Actor = actor.Entity, .Key = keyOf(i), .Event = AbilityTraceEvent::BindingAmbiguous });
                break;
            }
            const std::size_t* exclusive = std::find_if(order.data() + at, order.data() + next, [&](std::size_t c) {
                return binding[c]->Consume == AbilityConsume::Exclusive;
            });
            if (exclusive == order.data() + next)
            {
                at = next;
                continue;
            }
            if (pass.TryStart(actor, grant[*exclusive], keyOf(*exclusive), action, EntityId{}) >= 0)
                break;
            if (binding[*exclusive]->WhenBlocked == AbilityWhenBlocked::Consume)
            {
                pass.Trace({ .Actor = actor.Entity, .Key = keyOf(*exclusive), .Event = AbilityTraceEvent::Blocked });
                break;
            }
            at = next;
        }
        for (std::size_t i = 0; i < count; ++i)
            if (binding[i]->Consume == AbilityConsume::Observe)
                (void)pass.TryStart(actor, grant[i], keyOf(i), action, EntityId{});
    }

    // A release, genuine or not, reaches only the activations its action started.
    void Release(AbilityPass& pass, const AbilityActor& actor, const AbilityGrantEntries& entries, std::uint8_t action,
                 bool interrupted)
    {
        for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
        {
            AbilityRecord& record = actor.Record(slot);
            if (record.State != AbilityRecordState::Live || record.TriggerAction != action)
                continue;
            const AbilityEffectiveBinding* binding = RecordBinding(entries, actor, record);
            if (binding == nullptr || binding->Policy == AbilityInputPolicy::Press)
                continue;
            if (interrupted && binding->OnInterrupt == AbilityInterrupt::Cancel)
            {
                pass.End(actor, slot, AbilityEndReason::InputInterrupted);
                continue;
            }
            // A toggle ends on its next press; its release is the key coming up.
            if (binding->Policy == AbilityInputPolicy::Toggle && !interrupted)
                continue;
            record.Released = true;
            pass.Run(actor, slot);
        }
    }

    void Requests(AbilityPass& pass, World& world, const StoragePartitionSet* partitions, AbilityPassScratch& scratch)
    {
        AbilityRequestQueue* queue = world.TryGetResource<AbilityRequestQueue>();
        if (queue == nullptr || queue->Pending.empty())
            return;
        scratch.Requests.swap(queue->Pending);
        queue->Pending.clear();
        for (std::size_t i = 0; i < scratch.Requests.size(); ++i)
        {
            const AbilityRequest& request = scratch.Requests[i];
            if (!world.IsAlive(request.Actor) || !world.HasComponent<ActiveAbilities>(request.Actor)
                || !world.HasComponent<AbilityLocals>(request.Actor))
                continue;
            // A dormant actor keeps its request for the tick it wakes.
            if (partitions != nullptr && !partitions->Contains(world.GetEntityPartition(request.Actor)))
            {
                (void)queue->Push(request);
                continue;
            }
            const AbilityActor actor{ &world, request.Actor, {}, kLocalInputActionSource };
            if (request.Kind == AbilityRequestKind::Cancel)
            {
                for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
                    if (actor.Record(slot).State == AbilityRecordState::Live
                        && actor.Record(slot).Key.Ability == request.Ability)
                        pass.End(actor, slot, AbilityEndReason::ExplicitCancel);
                continue;
            }
            const AbilityActivationKey key{
                .CommandTick = pass.Tick(), .Ability = request.Ability,
                .Origin = static_cast<std::uint8_t>(kAbilityRequestOrigin + std::min<std::size_t>(i, 0x7F)) };
            const AbilityGrants* grants = actor.Grants();
            const AbilityGrant* found = nullptr;
            for (std::uint8_t g = 0; grants != nullptr && g < grants->Count && found == nullptr; ++g)
                if (grants->Entries[g].Ability == request.Ability)
                    found = &grants->Entries[g];
            if (found == nullptr)
            {
                pass.Trace({ .Actor = request.Actor, .Key = key, .Event = AbilityTraceEvent::Refused,
                             .Refusal = AbilityRefusal::NotGranted });
                continue;
            }
            const AbilityGrant grant = *found;
            (void)pass.TryStart(actor, grant, key, kAbilityNoTrigger, request.Instigator);
        }
        scratch.Requests.clear();
    }

    // An actor holds an effect's grants while the effect lasts: granted the pass after
    // it applies, revoked the pass after it ends, so both machines agree on the tick.
    void SyncEffectGrants(World& world, const StoragePartitionSet* partitions, AbilityPassScratch& scratch)
    {
        const AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
        const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
        if (table == nullptr || table->Empty() || entries == nullptr || !world.IsRegistered<ActiveEffect>())
            return;
        if (!scratch.Effects)
            scratch.Effects.emplace(world);
        scratch.EffectGrants.clear();
        scratch.Effects->ForEachChunk([&](auto& view) {
            const auto effects = view.template Read<ActiveEffect>();
            const EntityIndex* indices = view.Entities();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                for (const std::uint16_t entry : table->Of(effects[i].Def))
                    scratch.EffectGrants.push_back({ effects[i].Target, world.ResolveEntityIndex(indices[i]), entry });
        });
        for (const AbilityPassScratch::EffectGrant& pending : scratch.EffectGrants)
        {
            if (!world.IsAlive(pending.Target)
                || (partitions != nullptr && !partitions->Contains(world.GetEntityPartition(pending.Target))))
                continue;
            if (!world.HasComponent<AbilityGrants>(pending.Target))
                world.AddComponent<AbilityGrants>(pending.Target, AbilityGrants{});
            AbilityGrants& grants = *world.TryGet<AbilityGrants>(pending.Target);
            const bool held = std::any_of(grants.Entries, grants.Entries + grants.Count, [&](const AbilityGrant& grant) {
                return grant.Kind == AbilityGrantKind::Effect && grant.Effect == pending.Effect
                    && grant.Entry == pending.Entry;
            });
            if (!held)
                (void)GrantAbility(grants, entries->Find(pending.Entry)->Ability, AbilityGrantKind::Effect,
                                   pending.Entry, pending.Effect);
        }
    }

    void RevokeEndedEffectGrants(World& world, EntityId actor)
    {
        AbilityGrants& grants = *world.TryGet<AbilityGrants>(actor);
        for (std::uint8_t i = grants.Count; i-- > 0;)
        {
            const AbilityGrant& grant = grants.Entries[i];
            if (grant.Kind == AbilityGrantKind::Effect
                && (!world.IsAlive(grant.Effect) || !world.HasComponent<ActiveEffect>(grant.Effect)))
                (void)RevokeAbilityGrant(grants, grant.Id);
        }
    }

    void RefreshBindings(World& world, AbilityGrantEntries& entries, std::vector<std::string>& problems)
    {
        const AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
        if (abilities == nullptr || tags == nullptr)
            return;
        const InputActionVocabulary* vocabulary = world.TryGetResource<InputActionVocabulary>();
        problems.clear();
        entries.Refresh(*abilities, vocabulary != nullptr ? vocabulary->Actions : nullptr,
                        vocabulary != nullptr ? vocabulary->Revision : 0, *tags, problems);
    }
}

void RunAbilityPass(World& world, const AbilityHost& host, const FixedSimTime& now,
                    const StoragePartitionSet* partitions, const AbilitySettings& settings)
{
    if (!world.IsRegistered<AbilityGrants>())
        return;
    // A client runs only its guesses at its own pawn, on that pawn's command timeline.
    const bool authority = IsSimulationAuthority(world);
    const PredictedSimulation* predicted = authority ? nullptr : world.TryGetResource<PredictedSimulation>();
    if (!authority && (predicted == nullptr || !world.IsAlive(predicted->Entity)))
        return;
    AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    if (entries == nullptr)
        return;
    AbilityPassScratch& scratch = world.HasResource<AbilityPassScratch>() ? world.GetResource<AbilityPassScratch>()
                                                                          : world.AddResource<AbilityPassScratch>(world);
    RefreshBindings(world, *entries, scratch.Problems);
    if (host.Logging != nullptr)
        for (const std::string& problem : scratch.Problems)
            host.Logging->GetLogger<AbilitySystem>().Warn("{}", problem);
    if (authority)
        SyncEffectGrants(world, partitions, scratch);

    // Operations create and destroy entities, so the actors are gathered first and
    // nothing runs inside the query.
    scratch.Actors.clear();
    if (authority)
        scratch.Granted.ForEachChunk([&](auto& view) {
            if (partitions != nullptr && !partitions->Contains(view.Partition()))
                return;
            const EntityIndex* indices = view.Entities();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                scratch.Actors.push_back(world.ResolveEntityIndex(indices[i]));
        });
    else if (world.HasComponent<AbilityGrants>(predicted->Entity))
        scratch.Actors.push_back(predicted->Entity);

    FixedSimTime tick = now;
    if (!authority)
        tick.TickIndex = SimulationTickOf(world, predicted->Entity, now.TickIndex);
    AbilityPass pass(world, scratch.Buffers, host, tick, settings.StepBudget, settings.LingerTicks, !authority);
    const InputActionSources sources(world);
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const auto actorOf = [&](EntityId entity) {
        const InputActionSourceRef* ref =
            world.IsRegistered<InputActionSourceRef>() ? world.TryGet<InputActionSourceRef>(entity) : nullptr;
        return AbilityActor{ &world, entity, sources.TickFor(entity),
                             ref != nullptr ? ref->Source : kLocalInputActionSource };
    };
    const auto ready = [&](EntityId entity) {
        return world.IsAlive(entity) && world.HasComponent<ActiveAbilities>(entity)
            && world.HasComponent<AbilityLocals>(entity);
    };

    for (const EntityId entity : scratch.Actors)
    {
        if (!ready(entity))
            continue;
        if (authority)
            RevokeEndedEffectGrants(world, entity);
        pass.Sweep(actorOf(entity));
    }

    for (const EntityId entity : scratch.Actors)
    {
        if (!ready(entity))
            continue;
        const AbilityActor actor = actorOf(entity);
        const std::span<const InputActionEdge> edges = actor.Input.Edges;
        for (std::size_t e = 0; e < edges.size() && actor.Alive(); ++e)
        {
            const InputActionEdge edge = edges[e];
            if (edge.Kind == InputEdgeKind::Press)
                Press(pass, actor, *entries, tags, edge.Action, static_cast<std::uint8_t>(e));
            else
                Release(pass, actor, *entries, edge.Action, edge.Kind == InputEdgeKind::InterruptedRelease);
        }
    }

    if (authority)
        Requests(pass, world, partitions, scratch);

    for (const EntityId entity : scratch.Actors)
        if (ready(entity))
            pass.Advance(actorOf(entity));
    for (const EntityId entity : scratch.Actors)
        if (ready(entity))
            pass.Linger(actorOf(entity));

    if (AbilityPredictionJournal* journal = authority ? nullptr : world.TryGetResource<AbilityPredictionJournal>())
        for (const EntityId entity : scratch.Actors)
            journal->Note(world, entity);
}

AbilitySystem::AbilitySystem(AbilityHost host)
    : Host(host)
{
    if (Host.Console == nullptr)
        return;
    const auto setting = [&](const char* name, std::uint32_t& value, double minimum, const char* help) {
        (void)Host.Console->RegisterCVar({
            .Name = name,
            .Owner = "engine",
            .Type = CVarType::Int,
            .DefaultValue = static_cast<std::int64_t>(value),
            .CurrentValue = static_cast<std::int64_t>(value),
            .Flags = CVarFlags::None,
            .Help = help,
            .Source = { "engine" },
            .Min = minimum,
            .Max = 1024.0,
            .OnChange = [&value](const CVarChangeContext& ctx) {
                value = static_cast<std::uint32_t>(std::get<std::int64_t>(ctx.NewValue));
            },
        });
    };
    setting("ability.step_budget", Values.StepBudget, 1.0,
            "Phase transitions one activation may take in one tick; past it the activation ends StepBudget.");
    setting("ability.present_linger_ticks", Values.LingerTicks, 0.0,
            "Ticks an ended activation stays visible for presentation before its slot is freed.");
}

void AbilitySystem::FixedLogic(FixedLogicContext& ctx)
{
    RunAbilityPass(ctx.Entities, Host, ctx.Time, &ctx.Partitions, Values);
}
