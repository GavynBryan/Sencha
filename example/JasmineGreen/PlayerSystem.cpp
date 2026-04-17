#include "PlayerSystem.h"

#include <input/InputTypes.h>
#include <input/SdlInputSystem.h>

PlayerSystem::PlayerSystem(SdlInputSystem&         input,
                           EntityBatch<Player>&    players,
                           DataBatch<RigidBody2D>& bodies,
                           TransformStore<Transform2f>& transforms,
                           const Actions&          actions)
    : Input(input)
    , Players(players)
    , Bodies(bodies)
    , Transforms(transforms)
    , ActionIds(actions)
{
}

void PlayerSystem::Update(float /*dt*/)
{
    Vec2d direction    = {};
    Vec2d eyeDirection = {};

    for (const auto& event : Input.GetEvents().Items())
    {
        const bool active = event.Phase == InputPhase::Started
                         || event.Phase == InputPhase::Performed;

        if (active)
        {
            if (event.Action == ActionIds.MoveUp)        direction.Y += 1.0f;
            if (event.Action == ActionIds.MoveDown)      direction.Y -= 1.0f;
            if (event.Action == ActionIds.MoveLeft)      direction.X -= 1.0f;
            if (event.Action == ActionIds.MoveRight)     direction.X += 1.0f;
            if (event.Action == ActionIds.ShiftEyeLeft)  eyeDirection.X -= 1.0f;
            if (event.Action == ActionIds.ShiftEyeRight) eyeDirection.X += 1.0f;
        }

        if (event.Phase == InputPhase::Started && event.Action == ActionIds.Quit)
            QuitRequested = true;
    }

    if (direction.SqrMagnitude() > 0.0f)
        direction = direction.Normalized();

    const bool hasMovement    = direction.X    != 0.0f || direction.Y    != 0.0f;
    const bool hasEyeMovement = eyeDirection.X != 0.0f;

    if (!hasMovement && !hasEyeMovement)
        return;

    for (auto& player : Players.GetItems())
    {
        if (hasMovement)
        {
            if (RigidBody2D* body = Bodies.TryGet(player.Physics))
                body->Velocity = direction * Player::MoveSpeed;
        }

        if (hasEyeMovement)
        {
            if (Transform2f* eye = Transforms.TryGetLocalMutable(player.Eye))
                eye->Position.X += eyeDirection.X * 50.0f;
        }
    }
}
