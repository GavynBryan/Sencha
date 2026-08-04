#include <movement/MovementTuningResolutionSystem.h>

#include <app/GameContexts.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementComponents.h>
#include <movement/MovementDefs.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementProfileBindingCache.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>

#include <utility>

namespace
{
    constexpr float kFallbackMoveSpeed = 7.0f;

    float ReadMoveSpeed(const World& world, EntityId entity)
    {
        if (!world.IsRegistered<AttributeSet>())
            return kFallbackMoveSpeed;
        const AttributeSet* attributes = world.TryGet<AttributeSet>(entity);
        const MovementDefs* defs = world.TryGetResource<MovementDefs>();
        if (attributes == nullptr || defs == nullptr || !defs->MoveSpeed.IsValid())
            return kFallbackMoveSpeed;
        return attributes->GetCurrent(defs->MoveSpeed, kFallbackMoveSpeed);
    }
}

void MovementTuningResolutionSystem::Step(World& world)
{
    StepImpl(world, nullptr);
}

void MovementTuningResolutionSystem::StepImpl(World& world,
                                              const StoragePartitionSet* partitions)
{
    if (DataAssets == nullptr
        || !world.IsRegistered<CharacterMovement>()
        || !world.IsRegistered<ResolvedMovementTuning>())
    {
        return;
    }

    GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    LocomotionModeRegistry* modes = world.TryGetResource<LocomotionModeRegistry>();
    if (tags == nullptr || modes == nullptr)
        return;

    MovementProfileBindingCache* bindings = world.TryGetResource<MovementProfileBindingCache>();
    if (bindings == nullptr)
        bindings = &world.AddResource<MovementProfileBindingCache>(*DataAssets, *tags, *modes);

    const bool hasSupport = world.IsRegistered<SupportState>();
    const bool hasImmersion = world.IsRegistered<Immersion>();
    const bool hasTags = world.IsRegistered<GameplayTagContainer>();

    Query<Write<ResolvedMovementTuning>, Read<CharacterMovement>> query(world);
    const auto visit = [&](auto& view)
    {
        auto tunings = view.template Write<ResolvedMovementTuning>();
        const auto movements = view.template Read<CharacterMovement>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const EntityId entity = view.Entity(i);
            ResolvedMovementTuning& tuning = tunings[i];
            const CharacterMovement& movement = movements[i];

            const float maxSpeed = ReadMoveSpeed(std::as_const(world), entity);
            const BoundMovementProfile* profile = bindings->Get(movement.Profile);
            if (profile == nullptr)
            {
                // No profile bound: the attribute alone is the whole answer,
                // which keeps an unauthored character moving at a sane speed.
                tuning = ResolvedMovementTuning{};
                tuning.MaxSpeed = maxSpeed;
                continue;
            }

            MovementResolveContext context;
            context.Mode = movement.Mode;
            if (hasSupport)
            {
                if (const SupportState* support = std::as_const(world).TryGet<SupportState>(entity))
                    context.Support = support->Kind;
            }
            if (hasImmersion)
            {
                if (const Immersion* immersion = std::as_const(world).TryGet<Immersion>(entity))
                    context.Immersion = immersion->Fraction;
            }
            if (hasTags)
                context.Tags = std::as_const(world).TryGet<GameplayTagContainer>(entity);

            tuning = ResolveMovementTuning(*profile, context, maxSpeed, /*collectTrace*/ false).Tuning;
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}

void MovementTuningResolutionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions);
}
