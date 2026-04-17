#pragma once

#include <assets/texture/TextureHandle.h>
#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <core/handle/DataBatchHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/Collider2D.h>
#include <physics/2d/RigidBody2D.h>
#include <render/components/SpriteComponent.h>
#include <transform/TransformNode.h>
#include <transform/TransformSpace.h>

//=============================================================================
// Player
//
// The playable character. Body is a TransformNode that drives movement and
// participates in the EntityRegistry. Physics is a DataBatchHandle to a
// RigidBody2D — when the Player is destroyed the handle removes the body
// from the batch, which destructs the RigidBody2D and unregisters it from
// PhysicsDomain2D automatically.
//=============================================================================
struct Player
{
    static constexpr float MoveSpeed           = 200.0f; // Pixels per second
    static constexpr float CollisionHalfExtent = 24.0f;  // Half of the 48px body

    TransformNode2d                  Body;
    DataBatchHandle<RigidBody2D>     Physics;
    TransformNode2d                  Eye;
    DataBatchHandle<SpriteComponent> BodySprite;
    DataBatchHandle<SpriteComponent> EyeSprite;

    Player(TransformSpace2d& domain,
           Transform2f spawnTransform,
           PhysicsDomain2D& physics,
           DataBatch<RigidBody2D>& bodies,
           DataBatch<SpriteComponent>& sprites,
           TextureHandle texture)
        : Body(domain, spawnTransform)
        , Physics(bodies.Emplace(physics, Body.TransformKey(),
              Collider2D{ .HalfExtent = { CollisionHalfExtent, CollisionHalfExtent } }))
        , Eye(domain, Transform2f::Identity())
        , BodySprite(sprites.Emplace(domain))
        , EyeSprite(sprites.Emplace(domain))
    {
        Eye.SetParentByKey(Body.TransformKey());

        // Body sprite — 48x48 white quad
        if (SpriteComponent* s = sprites.TryGet(BodySprite))
        {
            s->Transform.SetParentByKey(Body.TransformKey());
            s->Texture  = texture;
            s->Width    = 48.0f;
            s->Height   = 48.0f;
            s->Color    = 0xFFFFFFFFu;
            s->SortKey  = 0;
        }

        // Eye sprite — 12x12 black quad, drawn on top
        if (SpriteComponent* s = sprites.TryGet(EyeSprite))
        {
            s->Transform.SetParentByKey(Eye.TransformKey());
            s->Texture  = texture;
            s->Width    = 12.0f;
            s->Height   = 12.0f;
            s->Color    = 0xFF000000u;
            s->SortKey  = 1;
        }
    }

    DataBatchKey TransformKey() const { return Body.TransformKey(); }
};
