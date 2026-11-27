#include <movement/MovementModeSystems.h>

#include <app/GameContexts.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagOverflow.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementTags.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <world/SimulationAuthority.h>
#include <world/SimulationTimeline.h>

#include <algorithm>
#include <cassert>
#include <span>
#include <utility>
#include <vector>

namespace
{
    bool ProjectsHere(const World& world, EntityId entity)
    {
        return IsSimulationAuthority(world) || IsLocallyPredicted(world, entity);
    }
}

void ModeRequestCollectionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void ModeRequestCollectionSystem::StepImpl(World& world, const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<GameplayTagContainer>()
        || !world.IsRegistered<ModeTransitionRequest>())
    {
        return;
    }

    const LocomotionModeRegistry* modes =
        std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (modes == nullptr)
        return;

    Query<Write<GameplayTagContainer>, With<ModeTransitionRequest>> query(world);
    const auto visit = [&](auto& view)
    {
        auto containers = view.template Write<GameplayTagContainer>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            GameplayTagContainer& tags = containers[i];
            for (const LocomotionModeEntry& mode : modes->Entries())
            {
                if (!mode.RequestTag.IsValid() || !tags.HasExact(mode.RequestTag))
                    continue;

                // The request tag is a one-tick mailbox: consume it here and
                // let arbitration own the outcome.
                (void)RequestLocomotionMode(world, view.Entity(i), mode.Id,
                                            ModeRequestClass::Explicit);
                tags.Erase(mode.RequestTag);
            }
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}

void ModeRequestCollectionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}

void LocomotionModeTransitionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void LocomotionModeTransitionSystem::StepImpl(World& world,
                                              const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<CharacterMovement>()
        || !world.IsRegistered<ModeTransitionRequest>()
        || !world.IsRegistered<GameplayTagContainer>())
    {
        return;
    }

    const LocomotionModeRegistry* modes =
        std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (modes == nullptr)
        return;

    struct PendingTransition
    {
        EntityId Entity;
        LocomotionModeId Previous;
        const LocomotionModeEntry* Target = nullptr;
        bool Rejected = false;
    };

    // Collected first, applied second: entry and exit hooks add and remove
    // session components, which is a structural change and must not happen
    // while a query is iterating.
    std::vector<PendingTransition> pending;

    Query<Write<ModeTransitionRequest>, Read<CharacterMovement>> collect(world);
    const auto gather = [&](auto& view)
    {
        auto requests = view.template Write<ModeTransitionRequest>();
        const auto movements = view.template Read<CharacterMovement>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            ModeTransitionRequest& request = requests[i];
            if (!request.Pending)
                continue;

            const EntityId entity = view.Entity(i);
            const LocomotionModeEntry* target = modes->Find(request.Target);

            // Evaluated before the live session is touched, so a mode that
            // staged a candidate can still see it.
            const bool canEnter = target != nullptr
                && (!target->CanEnter || target->CanEnter(std::as_const(world), entity));

            pending.push_back(PendingTransition{
                .Entity = entity,
                .Previous = movements[i].Mode,
                .Target = target,
                .Rejected = !canEnter,
            });

            request.Target = {};
            request.Class = ModeRequestClass::Automatic;
            request.Pending = false;
        }
    };

    if (partitions != nullptr)
        collect.ForEachChunkIn(*partitions, gather);
    else
        collect.ForEachChunk(gather);

    for (const PendingTransition& transition : pending)
    {
        if (!world.IsAlive(transition.Entity))
            continue;

        if (transition.Rejected || transition.Target == nullptr)
        {
            if (transition.Target != nullptr && transition.Target->DiscardCandidate)
                transition.Target->DiscardCandidate(world, transition.Entity);
            continue;
        }

        if (world.TryGet<CharacterMovement>(transition.Entity) == nullptr
            || world.TryGet<GameplayTagContainer>(transition.Entity) == nullptr)
        {
            continue;
        }

        if (transition.Previous == transition.Target->Id)
        {
            if (transition.Target->DiscardCandidate)
                transition.Target->DiscardCandidate(world, transition.Entity);
        }
        else
        {
            if (const LocomotionModeEntry* previous = modes->Find(transition.Previous);
                previous != nullptr && previous->ExitSession)
            {
                previous->ExitSession(world, transition.Entity);
            }

            if (transition.Target->EnterSession)
                transition.Target->EnterSession(world, transition.Entity);
        }

        if (CharacterMovement* movement = world.TryGet<CharacterMovement>(transition.Entity))
            movement->Mode = transition.Target->Id;
    }
}

void LocomotionModeTransitionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}

void ModeTagProjectionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void ModeTagProjectionSystem::StepImpl(World& world, const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<CharacterMovement>() || !world.IsRegistered<GameplayTagContainer>())
        return;
    const LocomotionModeRegistry* modes = std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (modes == nullptr)
        return;

    const auto stacksFor = [](const LocomotionModeEntry& entry, LocomotionModeId mode) {
        return static_cast<std::uint16_t>(entry.Id == mode ? 1 : 0);
    };
    const auto settled = [&](const GameplayTagContainer& tags, LocomotionModeId mode) {
        return std::ranges::all_of(modes->Entries(), [&](const LocomotionModeEntry& entry) {
            return !entry.ActiveTag.IsValid() || tags.ContributedCount(entry.ActiveTag) == stacksFor(entry, mode);
        });
    };

    const auto refused = [&](EntityId entity, const GameplayTagContainer& tags, LocomotionModeId mode) {
        GameplayTagId adding;
        for (const LocomotionModeEntry& entry : modes->Entries())
        {
            if (!entry.ActiveTag.IsValid())
                continue;
            const std::uint16_t held = tags.ContributedCount(entry.ActiveTag);
            if (held > stacksFor(entry, mode))
                return false;
            if (held < stacksFor(entry, mode))
                adding = entry.ActiveTag;
        }
        return adding.IsValid() && !AdmitGameplayTags(world, entity, tags, std::span(&adding, 1));
    };

    std::vector<EntityId> unsettled;
    Query<Read<CharacterMovement>, Read<GameplayTagContainer>> query(world);
    const auto visit = [&](auto& view)
    {
        const auto movements = view.template Read<CharacterMovement>();
        const auto containers = view.template Read<GameplayTagContainer>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            if (!settled(containers[i], movements[i].Mode) && ProjectsHere(world, view.Entity(i))
                && !refused(view.Entity(i), containers[i], movements[i].Mode))
                unsettled.push_back(view.Entity(i));
    };
    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);

    for (const EntityId entity : unsettled)
    {
        const LocomotionModeId mode = std::as_const(world).TryGet<CharacterMovement>(entity)->Mode;
        GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(entity);
        for (const bool adding : { false, true })
            for (const LocomotionModeEntry& entry : modes->Entries())
            {
                const std::uint16_t wanted = stacksFor(entry, mode);
                if (!entry.ActiveTag.IsValid() || (wanted > tags.ContributedCount(entry.ActiveTag)) != adding)
                    continue;
                assert(tags.BaselineCount(entry.ActiveTag) == 0 && "a projected tag is never authored");
                if (tags.SetContribution(entry.ActiveTag, wanted) == GameplayTagWrite::Full)
                    NoteGameplayTagOverflow(world, entity, entry.ActiveTag);
            }
    }
}

void ModeTagProjectionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}

void SupportTagProjectionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void SupportTagProjectionSystem::StepImpl(World& world, const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<SupportState>()
        || !world.IsRegistered<GameplayTagContainer>())
    {
        return;
    }

    const MovementTags* movementTags = std::as_const(world).TryGetResource<MovementTags>();
    if (movementTags == nullptr || !movementTags->Grounded.IsValid())
        return;

    const GameplayTagId grounded = movementTags->Grounded;
    std::vector<EntityId> unsettled;
    Query<Read<SupportState>, Read<GameplayTagContainer>> query(world);
    const auto visit = [&](auto& view)
    {
        const auto supports = view.template Read<SupportState>();
        const auto containers = view.template Read<GameplayTagContainer>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const std::uint16_t stacks = supports[i].Kind == SupportKind::Stable ? 1 : 0;
            const std::uint16_t held = containers[i].ContributedCount(grounded);
            if (held != stacks && ProjectsHere(world, view.Entity(i))
                && (held > stacks || AdmitGameplayTags(world, view.Entity(i), containers[i], std::span(&grounded, 1))))
                unsettled.push_back(view.Entity(i));
        }
    };
    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);

    for (const EntityId entity : unsettled)
    {
        const bool stable = std::as_const(world).TryGet<SupportState>(entity)->Kind == SupportKind::Stable;
        GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(entity);
        assert(tags.BaselineCount(grounded) == 0 && "a projected tag is never authored");
        (void)tags.SetContribution(grounded, stable ? 1 : 0);
    }
}

void SupportTagProjectionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}
