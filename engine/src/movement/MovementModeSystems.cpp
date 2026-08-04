#include <movement/MovementModeSystems.h>

#include <app/GameContexts.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementComponents.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementTags.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>

#include <utility>
#include <vector>

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
                tags.Revoke(mode.RequestTag);
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

        CharacterMovement* movement = world.TryGet<CharacterMovement>(transition.Entity);
        GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(transition.Entity);
        if (movement == nullptr || tags == nullptr)
            continue;
        movement->Mode = transition.Target->Id;

        for (const LocomotionModeEntry& mode : modes->Entries())
        {
            if (mode.ActiveTag.IsValid() && mode.Id != transition.Target->Id)
                tags->Revoke(mode.ActiveTag);
        }
        if (transition.Target->ActiveTag.IsValid()
            && !tags->HasExact(transition.Target->ActiveTag))
        {
            tags->Grant(transition.Target->ActiveTag);
        }
    }
}

void LocomotionModeTransitionSystem::FixedLogic(FixedLogicContext& ctx)
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

    Query<Read<SupportState>, Write<GameplayTagContainer>> query(world);
    const auto visit = [&](auto& view)
    {
        const auto supports = view.template Read<SupportState>();
        auto containers = view.template Write<GameplayTagContainer>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            GameplayTagContainer& tags = containers[i];
            const bool grounded = supports[i].Kind == SupportKind::Stable;
            const bool held = tags.HasExact(movementTags->Grounded);
            if (grounded && !held)
                tags.Grant(movementTags->Grounded);
            else if (!grounded && held)
                tags.Revoke(movementTags->Grounded);
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}

void SupportTagProjectionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}
