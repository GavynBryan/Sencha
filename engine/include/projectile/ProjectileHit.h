#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredValue.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <string_view>

inline constexpr std::string_view kProjectileHitEvent = "projectile.hit";

//=============================================================================
// ProjectileHit
//
// Something was struck: delivered at the entity struck, for whatever responses
// it has (docs/gameplay/responses.md). The kind is the projectile's hit kind, so
// a response that should answer one kind of shot reads a tag rather than a
// different event. Splash is not a strike, and is not delivered.
//
// The occurrence does not need a projectile to make it. A hitscan weapon that
// means the same thing to what it hits delivers the same struct, with no
// projectile entity.
//=============================================================================
struct ProjectileHit
{
    EntityId Instigator{};
    EntityId Projectile{};
    GameplayTagId Kind{};
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
};

template <>
struct AuthoredApiDefinition<ProjectileHit>
{
    static constexpr std::string_view EventName = kProjectileHitEvent;
    static void Encode(const ProjectileHit& event, AuthoredArguments& payload)
    {
        payload.Resize(5);
        payload.Set(0, AuthoredValue::Entity(event.Instigator));
        payload.Set(1, AuthoredValue::Entity(event.Projectile));
        payload.Set(2, AuthoredValue::Tag(event.Kind));
        payload.Set(3, AuthoredValue::Vector({ { event.Point.X, event.Point.Y, event.Point.Z }, 3 }));
        payload.Set(4, AuthoredValue::Vector({ { event.Normal.X, event.Normal.Y, event.Normal.Z }, 3 }));
    }
};
