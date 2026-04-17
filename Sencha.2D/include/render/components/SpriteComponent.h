#pragma once

#include <assets/texture/TextureHandle.h>
#include <transform/TransformNode.h>
#include <transform/TransformSpace.h>
#include <cstdint>

//=============================================================================
// SpriteComponent
//
// Visual component for 2D entities. Owns a TransformNode2d whose world
// transform is the GPU submission position each frame.
//
// Attaching to an entity:
//   sprite.Transform.SetParentByKey(entity.TransformKey());
//
// The LocalOffset constructor argument sets the initial local transform
// relative to the parent (e.g. a centered eye, a dropped-shadow offset).
// Omit it for a sprite that sits exactly at the entity origin.
//=============================================================================
struct SpriteComponent
{
    TransformNode2d Transform;
    TextureHandle   Texture;
    float           Width   = 16.0f;
    float           Height  = 16.0f;
    uint32_t        Color   = 0xFFFFFFFFu;
    int32_t         SortKey = 0;

    SpriteComponent(TransformSpace2d& space,
                    const Transform2f& localOffset = Transform2f::Identity())
        : Transform(space, localOffset)
    {}
};
