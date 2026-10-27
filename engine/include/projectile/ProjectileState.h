#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class World;

// Entities one projectile may strike in its life, directly or by splash.
inline constexpr std::size_t kProjectileMaxHits = 16;

//=============================================================================
// ProjectileFlight
//
// What every machine knows about a launched projectile: who launched it, and
// the authority ticks it began and ended on. Replicated, through a codec for the
// instigator's identity; the pose travels as the entity's LocalTransform, as any
// mirrored entity's does.
//
// The two ticks are what let a machine that is drawing the world in the past
// draw a projectile only for the stretch of that past it existed in: not at its
// muzzle before it was launched, and not at the wall after it struck.
//=============================================================================
struct SENCHA_COMPONENT("sencha.projectile_flight") ProjectileFlight
{
    EntityId Instigator{};
    // Authority ticks. LaunchTick is the tick of the first step it flew; EndTick
    // is zero while it flies.
    std::uint64_t LaunchTick = 0;
    std::uint64_t EndTick = 0;
    // This machine's own: whether its meshes are drawn now. Not on the wire.
    bool Shown = true;
};

static_assert(std::is_trivially_copyable_v<ProjectileFlight>,
              "ProjectileFlight must be trivially copyable to live in ECS chunks");

//=============================================================================
// ProjectileMotion
//
// The authority's working state for a projectile still in flight, removed when
// it ends. Never replicated: no other machine moves it or decides what it meets.
//=============================================================================
struct SENCHA_COMPONENT("sencha.projectile_motion") ProjectileMotion
{
    Vec3d Velocity = Vec3d::Zero();
    // Seconds of flight left.
    float Remaining = 0.0f;
    // What the launcher measured: scales the scaled effect, and the splash.
    float Magnitude = 0.0f;
    float SplashScale = 1.0f;
    // Index into the World's ProjectileDefinitions.
    std::uint32_t Definition = 0;
    // Everything struck so far, each at most once.
    std::array<EntityId, kProjectileMaxHits> Hits{};
    std::uint8_t HitCount = 0;
};

static_assert(std::is_trivially_copyable_v<ProjectileMotion>,
              "ProjectileMotion must be trivially copyable to live in ECS chunks");

inline constexpr std::size_t kProjectileDefaultCapacity = 256;

// How many projectiles are in flight in a World, kept by ProjectileMotion's own
// lifetime so a zone that unloads with projectiles in it gives their places back,
// and how many may be (projectile.capacity): one limit, read alike by a launch
// and by whatever admits one ahead of it.
struct ProjectileCount
{
    std::size_t InFlight = 0;
    std::size_t Capacity = kProjectileDefaultCapacity;
};

#if !defined(SENCHA_CODEGEN)
template <>
struct ComponentTraits<ProjectileMotion>
{
    static void OnAdd(ProjectileMotion&, World& world, EntityId);
    static void OnRemove(const ProjectileMotion&, World& world, EntityId);
};
#endif

#if !defined(SENCHA_CODEGEN)
#  include <projectile/ProjectileState.sencha.h>
#  include <projectile/ProjectileFlightWire.h>
#endif
