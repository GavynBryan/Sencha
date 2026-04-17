#pragma once

#include <assets/texture/TextureHandle.h>
#include <core/batch/DataBatch.h>
#include <core/handle/DataBatchHandle.h>
#include <entity/EntityHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/Collider2D.h>
#include <physics/2d/RigidBody2D.h>
#include <sprite/SpriteComponent.h>
#include <transform/TransformSpace.h>

//=============================================================================
// Player
//
// The playable character. Body is the entity handle that drives movement and
// participates in the EntityRegistry. Physics is a DataBatchHandle to a
// RigidBody2D — when the Player is destroyed the handle removes the body
// from the batch, which destructs the RigidBody2D and unregisters it from
// PhysicsDomain2D automatically.
//=============================================================================
struct Player
{
    static constexpr float MoveSpeed           = 200.0f; // Pixels per second
    static constexpr float CollisionHalfExtent = 24.0f;  // Half of the 48px body

    EntityHandle                     Body;
    DataBatchHandle<RigidBody2D>     Physics;
    EntityHandle                     Eye;

    Player(EntityHandle body,
           EntityHandle eye,
           TransformSpace2d& domain,
           Transform2f spawnTransform,
           PhysicsDomain2D& physics,
           DataBatch<RigidBody2D>& bodies,
           SpriteStore& sprites,
           TextureHandle texture)
        : Body(body)
        , Physics(bodies.Emplace(physics, Body,
              Collider2D{ .HalfExtent = { CollisionHalfExtent, CollisionHalfExtent } }))
        , Eye(eye)
    {
        domain.Transforms.Add(Body, spawnTransform);
        domain.Transforms.Add(Eye, Transform2f::Identity());
        domain.Hierarchy.SetParent(Eye, Body);

        // Body sprite — 48x48 white quad
        sprites.Add(Body);
        if (SpriteComponent* s = sprites.TryGet(Body))
        {
            s->Texture  = texture;
            s->Width    = 48.0f;
            s->Height   = 48.0f;
            s->Color    = 0xFFFFFFFFu;
            s->SortKey  = 0;
        }

        // Eye sprite — 12x12 black quad, drawn on top
        sprites.Add(Eye);
        if (SpriteComponent* s = sprites.TryGet(Eye))
        {
            s->Texture  = texture;
            s->Width    = 12.0f;
            s->Height   = 12.0f;
            s->Color    = 0xFF000000u;
            s->SortKey  = 1;
        }
    }

    EntityHandle Handle() const { return Body; }
};
