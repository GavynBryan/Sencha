#include "PlayerSystem.h"

#include <input/InputTypes.h>
#include <input/SdlInputSystem.h>
#include <math/geometry/2d/Aabb2d.h>
#include <physics/2d/PhysicsDomain2D.h>

#include <cmath>

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

    for (const auto& event : Input.GetEvents().Items())
    {
        const bool active = event.Phase == InputPhase::Started
                         || event.Phase == InputPhase::Performed;

        if (active)
        {
            if (event.Action == ActionIds.MoveUp)    direction.Y -= 1.0f;
            if (event.Action == ActionIds.MoveDown)  direction.Y += 1.0f;
            if (event.Action == ActionIds.MoveLeft)  direction.X -= 1.0f;
            if (event.Action == ActionIds.MoveRight) direction.X += 1.0f;
        }

        if (event.Phase == InputPhase::Started && event.Action == ActionIds.Quit)
            QuitRequested = true;
    }

    if (direction.SqrMagnitude() > 0.0f)
        direction = direction.Normalized();

    const Vec2d desiredDelta = direction * (Player::MoveSpeed * dt);

    for (auto& player : Players.GetItems())
    {
        auto& pos = player.Body.Local().Position;

        const Aabb2d bounds = Aabb2d::FromCenterHalfExtent(
            Vec2d{pos.X, pos.Y},
            Vec2d{Player::CollisionHalfExtent, Player::CollisionHalfExtent});

        const MoveResult2D result = Physics.MoveBox(bounds, desiredDelta);

        pos.X += result.ResolvedDelta.X;
        pos.Y += result.ResolvedDelta.Y;
    }
}
