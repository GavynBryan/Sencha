#include "CharacterInputSystem.h"

#include "TemplateInputActions.h"

#include <app/GameContexts.h>
#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <input/InputActionSource.h>
#include <math/Quat.h>
#include <math/Vec.h>
#include <movement/MovementIntent.h>
#include <movement/MovementTags.h>

#include <cmath>
#include <cstdint>

void CharacterInputSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<GameplayTagContainer>())
    {
        return;
    }

    const MovementTags* tags =
        world.TryGetResource<MovementTags>();
    if (tags == nullptr)
        return;

    const TemplateInputActions* actionIds =
        world.TryGetResource<TemplateInputActions>();
    if (actionIds == nullptr)
        return;

    // Every controlled entity steers from its own input source: this
    // machine's devices for the player sitting here, a peer's arriving
    // commands for everyone else. Resolving one action state for the whole
    // pass is what would make one player's keys move every pawn at once.
    const InputActionSources sources(world);

    // Each controlled entity steers along its own aim, read from the entity
    // rather than from whatever camera happens to be watching it.
    Query<
        Write<MovementIntent>,
        Read<GameplayTagContainer>,
        Read<LookOrientation>> query(world);
    query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
    {
        auto intents = view.template Write<MovementIntent>();
        const auto entityTags =
            view.template Read<GameplayTagContainer>();
        const auto orientations =
            view.template Read<LookOrientation>();
        for (std::uint32_t index = 0;
             index < view.Count();
             ++index)
        {
            if (!entityTags[index].HasExact(tags->Controlled))
                continue;

            const EntityId steered = view.Entity(index);

            // This tick's actions, not the frame's: a frame that runs
            // several ticks steers each of them, and one that runs none
            // steers nothing.
            const InputActionView input = sources.TickFor(steered);
            const Vec2d move = input.Axis2(actionIds->Move);
            const float strafe = move.X;
            const float forward = move.Y;

            // Whichever moment the action set authored. Jump authors "while
            // held": asking every tick the control is down means a press
            // just before landing fires on the first grounded tick, and
            // holding it hops again on each landing. The gate in the
            // movement step (on the ground, off cooldown) rejects the rest
            // for free.
            const bool jump = input.Fired(actionIds->Jump);

            // A body steers on the ground plane whatever it is looking at:
            // yaw frames the wish, pitch does not.
            const Quatf frame =
                Quatf::FromAxisAngle(Vec3d::Up(), orientations[index].Yaw);
            Vec3d wish =
                frame.RotateVector(Vec3d::Forward()) * forward
                + frame.RotateVector(Vec3d::Right()) * strafe;
            wish.Y = 0.0f;
            const float squared = wish.SqrMagnitude();
            if (squared > 1.0f)
                wish = wish * (1.0f / std::sqrt(squared));

            intents[index].WishDir = wish;
            intents[index].Jump = jump;
        }
    });
}
