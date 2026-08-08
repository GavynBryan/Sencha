#include <movement/JumpExecutionSystem.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <movement/MotionComposition.h>

#include <algorithm>
#include <cstdint>

bool StepJump(const SupportState& support,
              const ResolvedMovementTuning& tuning,
              bool jumpRequested,
              float deltaSeconds,
              JumpState& state,
              float& upVelocityOut)
{
    // Counts down whether or not a jump was asked for, so a step is a step.
    state.CooldownRemaining =
        std::max(0.0f, state.CooldownRemaining - deltaSeconds);

    if (!jumpRequested)
        return false;
    if (support.Kind != SupportKind::Stable)
        return false;
    if (state.CooldownRemaining > 0.0f)
        return false;

    state.CooldownRemaining = tuning.JumpCooldownSeconds;
    upVelocityOut = tuning.JumpSpeed;
    return true;
}

void JumpExecutionSystem::Step(World& world, float deltaSeconds)
{
    Step(world, nullptr, deltaSeconds);
}

void JumpExecutionSystem::Step(
    World& world,
    const StoragePartitionSet* partitions,
    float deltaSeconds)
{
    if (!world.IsRegistered<JumpState>()
        || !world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<SupportState>()
        || !world.IsRegistered<ResolvedMovementTuning>()
        || !world.IsRegistered<MotionAxisOverride>())
    {
        return;
    }

    if (LastWorld != &world)
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    const auto visit = [&](auto& view)
    {
        auto states = view.template Write<JumpState>();
        const auto intents = view.template Read<MovementIntent>();
        const auto supports = view.template Read<SupportState>();
        const auto tuning = view.template Read<ResolvedMovementTuning>();

        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            float up = 0.0f;
            if (!StepJump(supports[i], tuning[i], intents[i].Jump, deltaSeconds,
                          states[i], up))
            {
                continue;
            }

            // First-write-wins: a jump loses to an action that already claimed
            // the up channel this tick. The cooldown is spent either way, which
            // is what stops a held key retrying every tick.
            (void)TrySetUpMotionOverride(world, view.Entity(i), up);
        }
    };

    if (partitions != nullptr)
        CachedQuery->ForEachChunkIn(*partitions, visit);
    else
        CachedQuery->ForEachChunk(visit);
}

void JumpExecutionSystem::FixedLogic(FixedLogicContext& ctx)
{
    Step(ctx.Entities, &ctx.Partitions,
         static_cast<float>(ctx.Time.DeltaSeconds));
}
