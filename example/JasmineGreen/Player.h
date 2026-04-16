#pragma once

#include <core/batch/DataBatchKey.h>
#include <math/geometry/2d/Transform2d.h>
#include <world/transform/TransformNode.h>
#include <world/transform/TransformSpace.h>

#include <cstdint>

//=============================================================================
// SpriteDesc
//
// Visual description of one square sprite. Stored as data on the entity so
// the render system reads what to draw without any hardcoded values.
// The world-space position is filled in at render time from the transform.
//=============================================================================
struct SpriteDesc
{
    float    Size    = 16.0f;
    uint32_t Color   = 0xFFFFFFFFu; // Packed RGBA8. 0xFFFFFFFF = opaque white.
    int32_t  SortKey = 0;
};

//=============================================================================
// Player
//
// The playable character. Represented visually as a white square (Body) with
// a small black square (Eye) centered on it.
//
// The Eye is a child transform of the Body. When the Body moves, the Eye
// follows automatically — no extra code needed. This is how hierarchy works
// in Sencha: parent transforms compose into children via
// TransformPropagationSystem each fixed step.
//
// Only the Body participates in the EntityRegistry. The Eye is a second
// TransformNode that is purely cosmetic — it lives in the same TransformSpace
// but is not an entity on its own.
//=============================================================================
struct Player
{
    static constexpr float MoveSpeed = 200.0f; // Pixels per second

    TransformNode2d Body;
    TransformNode2d Eye; // Parented to Body; local transform is zero (centered)

    SpriteDesc BodySprite = {.Size = 48.0f, .Color = 0xFFFFFFFF, .SortKey = 0};
    SpriteDesc EyeSprite  = {.Size = 12.0f, .Color = 0x000000FF, .SortKey = 1};

    // Emplace via EntityBatch<Player>::Emplace(world.Domain, spawnTransform).
    Player(TransformSpace2d& domain, Transform2f spawnTransform)
        : Body(domain, spawnTransform)
        , Eye(domain, Transform2f{}) // Zero local = center of parent
    {
        Eye.SetParent(Body);
    }

    // Required by the IsEntity concept so EntityBatch can link this struct
    // into the EntityRegistry under the Body's transform key.
    DataBatchKey TransformKey() const { return Body.TransformKey(); }
};
