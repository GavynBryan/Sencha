#include "PlayerSystem.h"

#include <input/InputTypes.h>
#include <input/SdlInputSystem.h>
#include <math/geometry/2d/Aabb2d.h>
#include <physics/2d/PhysicsDomain2D.h>

#include <cmath>
#include <utility>

PlayerSystem::PlayerSystem(SdlInputSystem& input,
                           EntityBatch<Player>& players,
                           PhysicsDomain2D& physics,
                           const Actions& actions)
    : Input(input)
    , Players(players)
    , Physics(physics)
    , ActionIds(actions)
{
}

void PlayerSystem::Update(float dt)
{
    Vec2d direction = {};
    Vec2d eyeDirection = {};

    for (const auto& event : Input.GetEvents().Items())
    {
        const bool active = event.Phase == InputPhase::Started
                         || event.Phase == InputPhase::Performed;

        if (active)
        {
            if (event.Action == ActionIds.MoveUp)    direction.Y += 1.0f;
            if (event.Action == ActionIds.MoveDown)  direction.Y -= 1.0f;
            if (event.Action == ActionIds.MoveLeft)  direction.X -= 1.0f;
            if (event.Action == ActionIds.MoveRight) direction.X += 1.0f;
            if (event.Action == ActionIds.ShiftEyeLeft)  eyeDirection.X -= 1.0f;
            if (event.Action == ActionIds.ShiftEyeRight) eyeDirection.X += 1.0f;
        }

        if (event.Phase == InputPhase::Started && event.Action == ActionIds.Quit)
            QuitRequested = true;
    }

    if (direction.SqrMagnitude() > 0.0f)
        direction = direction.Normalized();

    const Vec2d desiredDelta = direction * (Player::MoveSpeed * dt);

    const bool hasMovement = desiredDelta.X != 0.0f || desiredDelta.Y != 0.0f;
    const bool hasEyeMovement = eyeDirection.X != 0.0f;

    if (!hasMovement && !hasEyeMovement)
        return;

    for (auto& player : Players.GetItems())
    {
        if (hasMovement)
        {
            const Vec2d pos = std::as_const(player.Body).Local().Position;

            const Aabb2d bounds = Aabb2d::FromCenterHalfExtent(
                Vec2d{pos.X, pos.Y},
                Vec2d{Player::CollisionHalfExtent, Player::CollisionHalfExtent});

            const MoveResult2D result = Physics.MoveBox(bounds, desiredDelta);

            if (result.ResolvedDelta.X != 0.0f || result.ResolvedDelta.Y != 0.0f)
            {
                auto& mpos = player.Body.Local().Position;
                mpos.X += result.ResolvedDelta.X;
                mpos.Y += result.ResolvedDelta.Y;
            }
        }

        if (hasEyeMovement)
            player.Eye.Local().Position.X += eyeDirection.X * (50.0f * dt);
    }
}
