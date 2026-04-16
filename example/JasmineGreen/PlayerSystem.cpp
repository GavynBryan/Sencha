#include "PlayerSystem.h"

#include <input/InputTypes.h>
#include <input/SdlInputSystem.h>
#include <math/geometry/2d/Aabb2d.h>
#include <physics/2d/PhysicsDomain2D.h>

#include <cmath>

PlayerSystem::PlayerSystem(SdlInputSystem& input,
                           EntityBatch<Player>& players,
                           PhysicsDomain2D& physics,
                           SpriteFeature& sprites,
                           BindlessImageIndex whitePixel,
                           const Actions& actions)
    : Input(input)
    , Players(players)
    , Physics(physics)
    , Sprites(sprites)
    , WhitePixel(whitePixel)
    , ActionIds(actions)
{
}

void PlayerSystem::Update(float dt)
{
    // Build a direction vector from whatever movement keys are held this frame.
    // Held actions emit InputPhase::Performed every frame they remain held, and
    // InputPhase::Started on the frame they are first pressed.
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

    // Normalize so diagonal movement isn't faster than cardinal.
    if (direction.SqrMagnitude() > 0.0f)
        direction = direction.Normalized();

    const Vec2d desiredDelta = direction * (Player::MoveSpeed * dt);

    for (auto& player : Players.GetItems())
    {
        auto& pos = player.Body.Local().Position;

        // Construct an AABB at the player's current position and ask the
        // physics domain to resolve the desired movement against any static
        // colliders. There are none yet — but this is the correct path so
        // walls and platforms added later need no changes here.
        const Aabb2d bounds = Aabb2d::FromCenterHalfExtent(
            Vec2d{pos.X, pos.Y},
            Vec2d{player.BodySprite.Size * 0.5f, player.BodySprite.Size * 0.5f});

        const MoveResult2D result = Physics.MoveBox(bounds, desiredDelta);

        pos.X += result.ResolvedDelta.X;
        pos.Y += result.ResolvedDelta.Y;
    }
}

void PlayerSystem::Render(float /*alpha*/)
{
    // TransformPropagationSystem has already run this fixed step, so
    // Body.World() and Eye.World() reflect the positions after movement.
    for (const auto& player : Players.GetItems())
    {
        const auto& bodyPos = player.Body.World().Position;
        const auto& eyePos  = player.Eye.World().Position;

        // The sprite parameters (size, color, sort order) live on the entity.
        // The render system's only job is to read that data and fill in the
        // world-space position from the transform.
        Sprites.Submit({
            .CenterX = bodyPos.X,
            .CenterY = bodyPos.Y,
            .Width   = player.BodySprite.Size,
            .Height  = player.BodySprite.Size,
            .Color   = player.BodySprite.Color,
            .Texture = WhitePixel,
            .SortKey = player.BodySprite.SortKey,
        });

        Sprites.Submit({
            .CenterX = eyePos.X,
            .CenterY = eyePos.Y,
            .Width   = player.EyeSprite.Size,
            .Height  = player.EyeSprite.Size,
            .Color   = player.EyeSprite.Color,
            .Texture = WhitePixel,
            .SortKey = player.EyeSprite.SortKey,
        });
    }
}
