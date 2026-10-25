#pragma once

#include <core/assets/AssetId.h>
#include <ecs/EntityId.h>
#include <ecs/Query.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>
#include <projectile/Projectile.h>
#include <projectile/ProjectileState.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AssetSystem;
class ConsoleRegistry;
class INetPrefabSpawner;
class LoggingProvider;
class NetTickEstimator;
class PhysicsWorld;
class ReplicationInterpolation;
class ResponsesSystem;
class World;
struct FixedLogicContext;
struct FixedSimTime;
struct CharacterController;
struct WorldTransform;

// A prefab's Projectile with its names resolved: what a flight reads every tick.
struct ResolvedProjectile
{
    AssetId Prefab{};
    Projectile Definition{};
    EffectId HitEffect{};
    EffectId ScaledEffect{};
    EffectId SplashEffect{};
    GameplayTagId PassesThrough{};
    GameplayTagId HitKind{};
    GameplayTagId ImpactCue{};
};

// Every prefab launched in this World, resolved once. A resource rather than the
// system's own so a flight's index into it survives whatever holds the system.
struct ProjectileDefinitions
{
    std::vector<ResolvedProjectile> Entries;
    std::unordered_map<AssetId, std::uint32_t> ByPrefab;
};

// One launch. Origin and direction are in world space; the direction need not
// be normalized.
struct ProjectileLaunch
{
    AssetId Prefab{};
    EntityId Instigator{};
    Vec3d Origin = Vec3d::Zero();
    Vec3d Direction = Vec3d::Forward();
    // The tick it is launched on, which is the first tick it flies.
    std::uint64_t Tick = 0;
    // Half-angle of the cone the direction is scattered within, in degrees.
    float SpreadDegrees = 0.0f;
    // Picks the scatter. The same seed scatters the same way on every machine.
    std::uint64_t Seed = 0;
    float SpeedScale = 1.0f;
    float Magnitude = 0.0f;
    float SplashScale = 1.0f;
};

enum class ProjectileLaunchResult : std::uint8_t
{
    Launched,
    // Not the simulation authority, no physics, or an instigator or aim that is
    // not usable.
    Refused,
    // As many projectiles as projectile.capacity allows are already in flight.
    Full,
    // The prefab is not one this build has, has no single root, does not carry a
    // Projectile, or names an effect or tag nobody declared.
    Unresolved,
};

[[nodiscard]] std::string_view ProjectileLaunchResultName(ProjectileLaunchResult result);

// Projectiles in flight in `world`; zero in a World that has never launched one.
[[nodiscard]] std::size_t ProjectilesInFlight(const World& world);

//=============================================================================
// ProjectileSystem
//
// Launches projectiles and, on the simulation authority, flies them: each tick
// a projectile sweeps its sphere along its path against bodies and character
// capsules, strikes what it meets in order, passes through what its tag lets it,
// and ends against anything else or when its time runs out -- splashing if it
// was made to. Every consequence is the authority's: effects, the projectile.hit
// delivery and the impact cue.
//
// On every other machine it only presents. A mirrored projectile's meshes are
// drawn while the tick this machine is presenting falls between its launch and
// its end, so a world drawn in the past shows the projectile only where it was.
// See docs/gameplay/projectiles.md.
//=============================================================================
class ProjectileSystem
{
public:
    struct Services
    {
        // Required for flight; without it nothing launches.
        const PhysicsWorld* Physics = nullptr;
        // Builds the prefab. The engine passes its replicated spawner, the same
        // one a client builds the mirrored prefab with.
        INetPrefabSpawner* Prefabs = nullptr;
        // Resolves a prefab named by path. Without it only AssetIds launch.
        AssetSystem* Assets = nullptr;
        // Where strikes are delivered; none in a host without responses.
        ResponsesSystem* Responses = nullptr;
        // How a client knows which authority tick it is presenting. Absent on a
        // machine that is never a client.
        const NetTickEstimator* Clock = nullptr;
        const ReplicationInterpolation* Interpolation = nullptr;
        ConsoleRegistry* Console = nullptr;
        LoggingProvider* Logging = nullptr;
    };

    ProjectileSystem(World& world, const Services& services);

    // The authority's only. Builds the prefab in the instigator's partition,
    // oriented along its path, and marks it for replication.
    EntityId Launch(const ProjectileLaunch& launch, ProjectileLaunchResult* result = nullptr);
    // The prefab a path names, or an invalid id when nothing resolves it.
    [[nodiscard]] AssetId PrefabOf(std::string_view path) const;
    // A scatter seed for `instigator`'s next launch on `tick`: from who, when,
    // and which of that instigator's launches this tick it is, so no other
    // instigator's launches change it. Each call counts one launch.
    [[nodiscard]] std::uint64_t NextSeed(EntityId instigator, std::uint64_t tick);

    void FixedLogic(FixedLogicContext& ctx);
    [[nodiscard]] const World& GetWorld() const { return *Entities; }

    // The most in flight at once, and the ticks an ended projectile is kept so
    // that its end reaches every peer (projectile.capacity, projectile.linger_ticks).
    void SetCapacity(std::size_t capacity) { Capacity = capacity; }
    [[nodiscard]] std::size_t GetCapacity() const { return Capacity; }
    void SetLingerTicks(std::uint32_t ticks) { LingerTicks = ticks; }
    [[nodiscard]] std::uint32_t GetLingerTicks() const { return LingerTicks; }

    // Diagnostics.
    [[nodiscard]] std::uint64_t Launched() const { return LaunchCount; }
    [[nodiscard]] std::uint64_t Strikes() const { return StrikeCount; }
    [[nodiscard]] std::uint64_t Refusals() const { return RefusalCount; }

private:
    struct Contact
    {
        EntityId Entity{};
        float Distance = 0.0f;
        Vec3d Point = Vec3d::Zero();
        Vec3d Normal = Vec3d::Zero();
    };

    std::optional<std::uint32_t> Resolve(AssetId prefab, EntityId root);
    void Fly(EntityId projectile, const FixedSimTime& now);
    // The nearest thing the sphere meets along `direction` within `distance`.
    std::optional<Contact> Sweep(const ProjectileMotion& motion, const ResolvedProjectile& definition,
                                 EntityId instigator, const Vec3d& from, const Vec3d& direction, float distance);
    std::optional<Contact> SweepCharacters(const ProjectileMotion& motion, const ResolvedProjectile& definition,
                                           EntityId instigator, const Vec3d& from, const Vec3d& direction,
                                           float distance);
    // Strikes `target`: its effects, its delivery, the cue. False if it was struck before.
    bool Strike(EntityId projectile, ProjectileMotion& motion, const ResolvedProjectile& definition,
                EntityId instigator, const Contact& contact, const FixedSimTime& now);
    void ApplyEffects(float magnitude, const ResolvedProjectile& definition, EntityId target,
                      EffectId override, const FixedSimTime& now);
    void Splash(ProjectileMotion& motion, const ResolvedProjectile& definition, EntityId instigator,
                const Vec3d& point, const Vec3d& normal, const FixedSimTime& now);
    void Cue(EntityId instigator, EntityId projectile, GameplayTagId cue, const Vec3d& point, const Vec3d& normal,
             EntityId target, std::uint64_t tick);
    void End(EntityId projectile, std::uint64_t tick);
    void Present(FixedLogicContext& ctx);
    void Show(EntityId projectile, bool shown);
    void RegisterConsole(ConsoleRegistry& console);

    World* Entities;
    Services Host;
    std::size_t Capacity = 256;
    std::uint32_t LingerTicks = 8;

    std::optional<Query<Read<ProjectileMotion>>> Flying;
    std::optional<Query<Write<ProjectileFlight>>> Flights;
    std::optional<Query<Read<CharacterController>, Read<WorldTransform>>> Characters;
    // Reused every tick: gathered before anything is struck, because a strike
    // can apply effects that make structural changes.
    std::vector<EntityId> Scratch;
    std::vector<EntityId> Overlaps;
    // Launches seeded this tick, per instigator.
    std::uint64_t SeedTick = 0;
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> SeedsThisTick;

    // The last tick flown, so a launch made without a tick (a console command)
    // is stamped with the one about to fly.
    std::uint64_t LastTick = 0;
    std::uint64_t LaunchCount = 0;
    std::uint64_t StrikeCount = 0;
    std::uint64_t RefusalCount = 0;
};

// Registers the system and its ordering against the ability pass, after the
// game's systems: projectiles fly the tick they are launched on.
ProjectileSystem& RegisterProjectileSystem(class EngineSchedule& schedule, World& world,
                                           const ProjectileSystem::Services& services);
