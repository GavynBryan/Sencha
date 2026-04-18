#pragma once

#include <assets/texture/TextureHandle.h>
#include <entity/EntityHandle.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/Collider2D.h>
#include <physics/RigidBody2D.h>
#include <sprite/SpriteComponent.h>
#include <sprite/SpriteStore.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformStore.h>

//=============================================================================
// Player
//
// The playable character. Body is the entity handle that drives movement and
// participates in the EntityRegistry. Physics is the same entity key used to
// look up the player's RigidBody2D in RigidBodyStore.
//=============================================================================
struct Player
{
    static constexpr float MoveSpeed           = 200.0f; // Pixels per second
    static constexpr float CollisionHalfExtent = 24.0f;  // Half of the 48px body

    EntityHandle                     Body;
    EntityHandle                     Physics;
    EntityHandle                     Eye;

    Player(EntityHandle body,
           EntityHandle eye,
           TransformStore<Transform2f>& transforms,
           TransformHierarchyService& hierarchy,
           Transform2f spawnTransform,
           RigidBodyStore& bodies,
           SpriteStore& sprites,
           TextureHandle texture)
        : Body(body)
        , Physics(Body)
        , Eye(eye)
    {
        bodies.Add(Body, Collider2D{ .HalfExtent = { CollisionHalfExtent, CollisionHalfExtent } });

        transforms.Add(Body, spawnTransform);
        transforms.Add(Eye, Transform2f::Identity());
        hierarchy.SetParent(Eye, Body);

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
