#include <projectile/ProjectileSystem.h>

#include <abilities/AbilityCueLog.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/runtime/AssetSystem.h>
#include <attributes/AttributeSet.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/ResponsesSystem.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <net/NetTickEstimator.h>
#include <net/ReplicationInterpolation.h>
#include <physics/CollisionShape.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <projectile/ProjectileHit.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <world/SimulationAuthority.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace
{
constexpr float kPi = 3.14159265358979f;
// How far past a contact a projectile that passes through resumes, so the next
// sweep does not begin inside what it just struck.
constexpr float kPassThroughStep = 0.001f;
// How far off a surface a splash looks for sight, so the surface itself does not
// block it.
constexpr float kSightLift = 0.02f;
// Seconds of lifetime too few to fly.
constexpr float kSpentLifetime = 1.0e-5f;

bool Finite(const Vec3d& v)
{
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

// The rotation that turns forward (+Z) onto `direction`. Direction alone does not
// fix roll, so the roll is whichever the shortest arc gives.
Quatf Facing(const Vec3d& direction)
{
    const Vec3d forward = Vec3d::Forward();
    const float cosine = std::clamp(forward.Dot(direction), -1.0f, 1.0f);
    const Vec3d axis = forward.Cross(direction);
    if (axis.SqrMagnitude() > 1.0e-8f)
        return Quatf::FromAxisAngle(axis.Normalized(), std::acos(cosine));
    return cosine < 0.0f ? Quatf::FromAxisAngle(Vec3d::Up(), kPi) : Quatf::Identity();
}

// Scatters `direction` uniformly over the cone of `halfAngleDegrees` around it,
// from `seed` alone, so every machine given the seed scatters it the same way.
Vec3d Scatter(const Vec3d& direction, float halfAngleDegrees, std::uint64_t seed)
{
    if (halfAngleDegrees <= 0.0f)
        return direction;
    // SplitMix64: two well-mixed draws from one seed.
    auto next = [&seed]() {
        seed += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) * (1.0f / 16777216.0f);
    };
    const float maxAngle = std::min(halfAngleDegrees, 90.0f) * (kPi / 180.0f);
    // Uniform over the cap: cos(theta) uniform between cos(max) and one.
    const float cosTheta = 1.0f - next() * (1.0f - std::cos(maxAngle));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = next() * 2.0f * kPi;
    Vec3d right = direction.Cross(Vec3d::Up());
    right = right.SqrMagnitude() < 1.0e-6f ? Vec3d::Right() : right.Normalized();
    const Vec3d up = right.Cross(direction).Normalized();
    return (direction * cosTheta + right * (std::cos(phi) * sinTheta) + up * (std::sin(phi) * sinTheta)).Normalized();
}

// Where a ray first enters a capsule standing upright about `centre`, as a
// distance along the ray, or nothing when it misses within `maxDistance`.
// A ray starting inside reports zero.
std::optional<float> RayCapsule(const Vec3d& origin, const Vec3d& direction, float maxDistance, const Vec3d& centre,
                                float halfSegment, float radius)
{
    const Vec3d a = centre - Vec3d::Up() * halfSegment;
    const Vec3d b = centre + Vec3d::Up() * halfSegment;
    const float radiusSq = radius * radius;

    // Inside already: the nearest point of the segment is within the radius.
    {
        const Vec3d ab = b - a;
        const float length = ab.SqrMagnitude();
        const float t = length > 0.0f ? std::clamp((origin - a).Dot(ab) / length, 0.0f, 1.0f) : 0.0f;
        if ((origin - (a + ab * t)).SqrMagnitude() <= radiusSq)
            return 0.0f;
    }

    std::optional<float> best;
    auto consider = [&](float t) {
        if (t >= 0.0f && t <= maxDistance && (!best || t < *best))
            best = t;
    };

    // The cylinder between the caps: solve in the plane across the up axis.
    const Vec3d up = Vec3d::Up();
    const Vec3d offset = origin - a;
    const Vec3d dPerp = direction - up * direction.Dot(up);
    const Vec3d oPerp = offset - up * offset.Dot(up);
    const float qa = dPerp.Dot(dPerp);
    if (qa > 1.0e-12f)
    {
        const float qb = 2.0f * oPerp.Dot(dPerp);
        const float qc = oPerp.Dot(oPerp) - radiusSq;
        const float disc = qb * qb - 4.0f * qa * qc;
        if (disc >= 0.0f)
        {
            const float t = (-qb - std::sqrt(disc)) / (2.0f * qa);
            const float along = (offset + direction * t).Dot(up);
            if (along >= 0.0f && along <= 2.0f * halfSegment)
                consider(t);
        }
    }
    // The caps.
    for (const Vec3d& cap : { a, b })
    {
        const Vec3d m = origin - cap;
        const float sb = m.Dot(direction);
        const float sc = m.Dot(m) - radiusSq;
        const float disc = sb * sb - sc;
        if (disc >= 0.0f)
            consider(-sb - std::sqrt(disc));
    }
    return best;
}

// The squared distance from `point` to an upright capsule's axis segment.
float SegmentDistanceSq(const Vec3d& point, const Vec3d& centre, float halfSegment)
{
    const float along = std::clamp((point - centre).Dot(Vec3d::Up()), -halfSegment, halfSegment);
    return (point - (centre + Vec3d::Up() * along)).SqrMagnitude();
}

float CapsuleHalfSegment(const CharacterController& character)
{
    return std::max(0.0f, character.Height * 0.5f - character.Radius);
}

bool Struck(const ProjectileMotion& motion, EntityId entity)
{
    return std::find(motion.Hits.begin(), motion.Hits.begin() + motion.HitCount, entity)
        != motion.Hits.begin() + motion.HitCount;
}
} // namespace

std::string_view ProjectileLaunchResultName(ProjectileLaunchResult result)
{
    switch (result)
    {
    case ProjectileLaunchResult::Launched: return "launched";
    case ProjectileLaunchResult::Refused: return "refused";
    case ProjectileLaunchResult::Full: return "full";
    case ProjectileLaunchResult::Unresolved: return "unresolved";
    }
    return "unknown";
}

std::size_t ProjectilesInFlight(const World& world)
{
    const ProjectileCount* count = world.TryGetResource<ProjectileCount>();
    return count != nullptr ? count->InFlight : 0;
}

std::size_t ProjectileCapacity(const World& world)
{
    const ProjectileCount* count = world.TryGetResource<ProjectileCount>();
    return count != nullptr ? count->Capacity : kProjectileDefaultCapacity;
}

void ComponentTraits<ProjectileMotion>::OnAdd(ProjectileMotion&, World& world, EntityId)
{
    if (ProjectileCount* count = world.TryGetResource<ProjectileCount>())
        ++count->InFlight;
}

void ComponentTraits<ProjectileMotion>::OnRemove(const ProjectileMotion&, World& world, EntityId)
{
    if (ProjectileCount* count = world.TryGetResource<ProjectileCount>(); count != nullptr && count->InFlight > 0)
        --count->InFlight;
}

ProjectileSystem::ProjectileSystem(World& world, const Services& services)
    : Entities(&world)
    , Host(services)
{
    if (world.TryGetResource<ProjectileCount>() == nullptr)
        world.AddResource<ProjectileCount>();
    if (world.TryGetResource<ProjectileDefinitions>() == nullptr)
        world.AddResource<ProjectileDefinitions>();
    Scratch.reserve(256);
    Overlaps.reserve(64);
    if (Host.Console != nullptr)
        RegisterConsole(*Host.Console);
}

AssetId ProjectileSystem::PrefabOf(std::string_view path) const
{
    if (Host.Assets == nullptr || path.empty())
        return AssetId{};
    const AssetRecord* record = Host.Assets->Resolve(path, AssetType::Scene);
    return record != nullptr ? record->Id : AssetId{};
}

std::uint64_t ProjectileSystem::NextSeed(EntityId instigator, std::uint64_t tick)
{
    if (tick != SeedTick)
    {
        SeedTick = tick;
        SeedsThisTick.clear();
    }
    const std::uint32_t ordinal = SeedsThisTick[instigator]++;
    std::uint64_t seed = (static_cast<std::uint64_t>(instigator.Index) << 32) | instigator.Generation;
    for (const std::uint64_t part : { tick, static_cast<std::uint64_t>(ordinal) })
    {
        seed ^= part + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
        seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ull;
        seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBull;
        seed ^= seed >> 31;
    }
    return seed;
}

std::optional<std::uint32_t> ProjectileSystem::Resolve(AssetId prefab, EntityId root)
{
    ProjectileDefinitions& definitions = Entities->GetResource<ProjectileDefinitions>();
    if (const auto found = definitions.ByPrefab.find(prefab); found != definitions.ByPrefab.end())
        return found->second;

    const Projectile* authored = std::as_const(*Entities).TryGet<Projectile>(root);
    if (authored == nullptr)
        return std::nullopt;

    ResolvedProjectile resolved{ .Prefab = prefab, .Definition = *authored };
    Projectile& d = resolved.Definition;
    std::string problem;
    if (!std::isfinite(d.Speed) || d.Speed <= 0.0f || !std::isfinite(d.Radius) || d.Radius < 0.0f
        || !std::isfinite(d.Lifetime) || d.Lifetime <= 0.0f || !std::isfinite(d.GravityScale)
        || !std::isfinite(d.SplashRadius) || d.SplashRadius < 0.0f)
        problem = "its speed and lifetime must be positive, and its radius, gravity and splash finite";
    d.MaxHits = std::clamp<std::uint32_t>(d.MaxHits, 1u, static_cast<std::uint32_t>(kProjectileMaxHits));

    const EffectRegistry* effects = Entities->TryGetResource<EffectRegistry>();
    auto effect = [&](const InlineString<192>& name, EffectId& out) {
        if (name.Empty() || !problem.empty())
            return;
        out = effects != nullptr ? effects->Find(name.View()) : EffectId{};
        if (effects == nullptr || effects->Get(out) == nullptr)
            problem = "no effect is declared as '" + std::string(name.View()) + "'";
    };
    effect(d.HitEffect, resolved.HitEffect);
    effect(d.ScaledEffect, resolved.ScaledEffect);
    effect(d.SplashEffect, resolved.SplashEffect);

    const GameplayTagRegistry* tags = Entities->TryGetResource<GameplayTagRegistry>();
    auto tag = [&](const InlineString<64>& name, GameplayTagId& out) {
        if (name.Empty() || !problem.empty())
            return;
        out = tags != nullptr ? tags->FindTag(name.View()) : GameplayTagId{};
        if (!out.IsValid())
            problem = "no gameplay tag is declared as '" + std::string(name.View()) + "'";
    };
    tag(d.PassesThrough, resolved.PassesThrough);
    tag(d.HitKind, resolved.HitKind);
    tag(d.ImpactCue, resolved.ImpactCue);

    if (!problem.empty())
    {
        if (Host.Logging != nullptr)
            Host.Logging->GetLogger<ProjectileSystem>().Error(
                "Projectile prefab {} will not launch: {}", AssetIdToString(prefab), problem);
        return std::nullopt;
    }

    const auto index = static_cast<std::uint32_t>(definitions.Entries.size());
    definitions.Entries.push_back(resolved);
    definitions.ByPrefab.emplace(prefab, index);
    return index;
}

EntityId ProjectileSystem::Launch(const ProjectileLaunch& launch, ProjectileLaunchResult* result)
{
    ProjectileLaunchResult ignored;
    ProjectileLaunchResult& outcome = result != nullptr ? *result : ignored;
    auto refuse = [&](ProjectileLaunchResult why) {
        outcome = why;
        ++RefusalCount;
        return EntityId{};
    };

    World& world = *Entities;
    if (!IsSimulationAuthority(world) || Host.Physics == nullptr || Host.Prefabs == nullptr)
        return refuse(ProjectileLaunchResult::Refused);
    if (!Finite(launch.Origin) || !Finite(launch.Direction) || launch.Direction.SqrMagnitude() < 1.0e-10f
        || !std::isfinite(launch.SpeedScale) || launch.SpeedScale <= 0.0f || !std::isfinite(launch.Magnitude)
        || !std::isfinite(launch.SplashScale) || launch.SplashScale < 0.0f || !std::isfinite(launch.SpreadDegrees))
        return refuse(ProjectileLaunchResult::Refused);
    if (launch.Instigator.IsValid() && !world.IsAlive(launch.Instigator))
        return refuse(ProjectileLaunchResult::Refused);
    if (ProjectilesInFlight(world) >= GetCapacity())
        return refuse(ProjectileLaunchResult::Full);

    const ProjectileDefinitions& definitions = world.GetResource<ProjectileDefinitions>();
    const auto known = definitions.ByPrefab.find(launch.Prefab);
    if (known == definitions.ByPrefab.end() && Host.Prefabs->Prepare(launch.Prefab) != NetPrefabReadiness::Ready)
        return refuse(ProjectileLaunchResult::Unresolved);

    const StoragePartitionId partition = launch.Instigator.IsValid()
        ? world.GetEntityPartition(launch.Instigator)
        : StoragePartitionId::Default();
    const EntityId root = Host.Prefabs->Instantiate(launch.Prefab, world, partition);
    if (!root.IsValid())
        return refuse(ProjectileLaunchResult::Unresolved);
    const std::optional<std::uint32_t> index = Resolve(launch.Prefab, root);
    if (!index.has_value())
    {
        Host.Prefabs->Despawn(world, root);
        return refuse(ProjectileLaunchResult::Unresolved);
    }
    const ResolvedProjectile& definition = world.GetResource<ProjectileDefinitions>().Entries[*index];

    const Vec3d direction = Scatter(launch.Direction.Normalized(), launch.SpreadDegrees, launch.Seed);
    if (LocalTransform* local = world.TryGet<LocalTransform>(root))
    {
        local->Value.Position = launch.Origin;
        local->Value.Rotation = Facing(direction);
    }
    // Its world pose is its local one: a projectile has no parent.
    if (WorldTransform* placed = world.TryGet<WorldTransform>(root))
    {
        if (const LocalTransform* local = std::as_const(world).TryGet<LocalTransform>(root))
            placed->Value = local->Value;
    }

    world.AddComponent<ProjectileFlight>(root, ProjectileFlight{ .Instigator = launch.Instigator,
                                                                 .LaunchTick = launch.Tick != 0 ? launch.Tick
                                                                                                : LastTick + 1,
                                                                 .EndTick = 0,
                                                                 .Shown = true });
    world.AddComponent<ProjectileMotion>(
        root, ProjectileMotion{ .Velocity = direction * (definition.Definition.Speed * launch.SpeedScale),
                                .Remaining = definition.Definition.Lifetime,
                                .Magnitude = launch.Magnitude,
                                .SplashScale = launch.SplashScale,
                                .Definition = *index });
    // Drawn between ticks, as anything moving every tick is; snapped, so it does
    // not streak from wherever the prefab was built to the muzzle.
    if (!world.HasComponent<WorldTransformHistory>(root))
        world.AddComponent<WorldTransformHistory>(root, WorldTransformHistory{});
    RequestTransformHistorySnap(world, root);
    // Every machine draws it: the prefab names its body, and the root alone
    // travels, as for any replicated group.
    if (!world.HasComponent<NetReplicated>(root))
        world.AddComponent<NetReplicated>(root, NetReplicated{});
    if (!world.HasComponent<NetSpawnPrefab>(root))
        world.AddComponent<NetSpawnPrefab>(root, NetSpawnPrefab{ .Scene = launch.Prefab });

    ++LaunchCount;
    outcome = ProjectileLaunchResult::Launched;
    return root;
}

void ProjectileSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = *Entities;
    LastTick = ctx.Time.TickIndex;
    if (!world.IsRegistered<ProjectileFlight>())
        return;
    if (!IsSimulationAuthority(world))
    {
        Present(ctx);
        return;
    }
    if (Host.Physics == nullptr)
        return;

    // Gathered first: a strike applies effects and delivers, either of which can
    // make structural changes.
    if (!Flying)
        Flying.emplace(world);
    Scratch.clear();
    Flying->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            Scratch.push_back(view.Entity(i));
    });
    for (const EntityId projectile : Scratch)
        if (world.IsAlive(projectile) && world.HasComponent<ProjectileMotion>(projectile))
            Fly(projectile, ctx.Time);

    // Ended projectiles are kept long enough for their end to reach every peer,
    // hidden and inert, then go.
    if (!Flights)
        Flights.emplace(world);
    Scratch.clear();
    const std::uint64_t now = ctx.Time.TickIndex;
    Flights->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        auto flights = view.template Write<ProjectileFlight>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            if (flights[i].EndTick != 0 && now >= flights[i].EndTick + LingerTicks)
                Scratch.push_back(view.Entity(i));
    });
    for (const EntityId ended : Scratch)
        if (world.IsAlive(ended))
            Host.Prefabs != nullptr ? Host.Prefabs->Despawn(world, ended) : world.DestroyEntity(ended);
}

void ProjectileSystem::Fly(EntityId projectile, const FixedSimTime& now)
{
    World& world = *Entities;
    const ProjectileDefinitions& definitions = world.GetResource<ProjectileDefinitions>();
    ProjectileMotion* motion = world.TryGet<ProjectileMotion>(projectile);
    const ProjectileFlight* flight = std::as_const(world).TryGet<ProjectileFlight>(projectile);
    LocalTransform* local = world.TryGet<LocalTransform>(projectile);
    if (motion == nullptr || flight == nullptr || local == nullptr || motion->Definition >= definitions.Entries.size())
    {
        End(projectile, now.TickIndex);
        return;
    }
    // Copied out of the World: a strike applies effects, and what they do to the
    // World is theirs to decide. The copy is written back when the tick is done.
    const ResolvedProjectile definition = definitions.Entries[motion->Definition];
    const Projectile& authored = definition.Definition;
    const EntityId instigator = flight->Instigator;
    ProjectileMotion state = *motion;

    if (authored.EndsWithInstigator && instigator.IsValid() && !world.IsAlive(instigator))
    {
        End(projectile, now.TickIndex);
        return;
    }

    const float dt = std::min(static_cast<float>(now.DeltaSeconds), state.Remaining);
    state.Remaining -= static_cast<float>(now.DeltaSeconds);
    const Vec3d gravity = Host.Physics->GetGravity() * authored.GravityScale;

    // The tick's path is the chord of its arc: exact for a straight flight, and
    // within a tick's curvature of the arc for a falling one.
    Vec3d position = local->Value.Position;
    const Vec3d start = position;
    const Vec3d target = position + state.Velocity * dt + gravity * (0.5f * dt * dt);
    state.Velocity += gravity * dt;
    const Vec3d chord = target - start;
    float remaining = chord.Magnitude();
    const Vec3d direction = remaining > 1.0e-6f ? chord / remaining : state.Velocity.Normalized();

    bool ended = false;
    Vec3d endNormal = -direction;
    while (remaining > 0.0f && !ended)
    {
        const std::optional<Contact> contact = Sweep(state, definition, instigator, position, direction, remaining);
        if (!contact.has_value())
        {
            position += direction * remaining;
            break;
        }
        position += direction * contact->Distance;
        remaining -= contact->Distance;

        const bool passes = contact->Entity.IsValid() && definition.PassesThrough.IsValid() && [&] {
            const GameplayTagContainer* tags =
                std::as_const(world).TryGet<GameplayTagContainer>(contact->Entity);
            return tags != nullptr && tags->HasExact(definition.PassesThrough);
        }();
        (void)Strike(projectile, state, definition, instigator, *contact, now);
        if (!world.IsAlive(projectile))
            return;
        if (passes && state.HitCount < authored.MaxHits)
        {
            const float step = std::min(remaining, kPassThroughStep);
            position += direction * step;
            remaining -= step;
            continue;
        }
        ended = true;
        endNormal = contact->Normal;
        Splash(state, definition, instigator, position, endNormal, now);
    }

    // A lifetime that is a whole number of ticks leaves rounding behind, not a
    // flight: what is left is less than any step could use.
    if (!ended && state.Remaining <= kSpentLifetime)
    {
        ended = true;
        if (authored.SplashOnExpire)
            Splash(state, definition, instigator, position, -direction, now);
        Cue(instigator, projectile, definition.ImpactCue, position, -direction, EntityId{}, now.TickIndex);
    }

    if (!world.IsAlive(projectile))
        return;
    local = world.TryGet<LocalTransform>(projectile);
    motion = world.TryGet<ProjectileMotion>(projectile);
    if (local == nullptr || motion == nullptr)
        return;
    *motion = state;
    local->Value.Position = position;
    if (state.Velocity.SqrMagnitude() > 1.0e-10f)
        local->Value.Rotation = Facing(state.Velocity.Normalized());
    if (ended)
        End(projectile, now.TickIndex);
}

std::optional<ProjectileSystem::Contact> ProjectileSystem::Sweep(const ProjectileMotion& motion,
                                                                 const ResolvedProjectile& definition,
                                                                 EntityId instigator, const Vec3d& from,
                                                                 const Vec3d& direction, float distance)
{
    std::array<EntityId, kProjectileMaxHits + 1> ignored{};
    std::size_t count = 0;
    if (instigator.IsValid())
        ignored[count++] = instigator;
    for (std::uint8_t i = 0; i < motion.HitCount; ++i)
        ignored[count++] = motion.Hits[i];
    const PhysicsQueryFilter filter{ .IncludeTriggers = false, .IgnoreEntities = { ignored.data(), count } };

    std::optional<Contact> nearest;
    const PhysicsQueries queries(*Host.Physics);
    if (definition.Definition.Radius > 0.0f)
    {
        const ShapeSweepHit hit = queries.SweepShape(CollisionShape::MakeSphere(definition.Definition.Radius), from,
                                                     Quatf::Identity(), direction, distance, filter);
        if (hit.Hit)
            nearest = Contact{ .Entity = hit.Entity,
                               .Distance = std::clamp(hit.Fraction, 0.0f, 1.0f) * distance,
                               .Point = hit.Point,
                               .Normal = hit.Normal };
    }
    else
    {
        const RaycastHit hit = queries.Raycast(from, direction, distance, filter);
        if (hit.Hit)
            nearest = Contact{ .Entity = hit.Entity, .Distance = hit.Distance, .Point = hit.Point, .Normal = hit.Normal };
    }

    if (definition.Definition.HitsCharacters)
        if (const std::optional<Contact> character =
                SweepCharacters(motion, definition, instigator, from, direction, distance);
            character.has_value() && (!nearest || character->Distance < nearest->Distance))
            nearest = character;
    return nearest;
}

std::optional<ProjectileSystem::Contact> ProjectileSystem::SweepCharacters(const ProjectileMotion& motion,
                                                                           const ResolvedProjectile& definition,
                                                                           EntityId instigator, const Vec3d& from,
                                                                           const Vec3d& direction,
                                                                           float distance)
{
    World& world = *Entities;
    if (!world.IsRegistered<CharacterController>())
        return std::nullopt;
    if (!Characters)
        Characters.emplace(world);

    std::optional<Contact> nearest;
    Characters->ForEachChunk([&](auto& view) {
        auto shapes = view.template Read<CharacterController>();
        auto poses = view.template Read<WorldTransform>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const EntityId entity = view.Entity(i);
            if (entity == instigator || Struck(motion, entity))
                continue;
            const Vec3d centre = poses[i].Value.Position;
            const float halfSegment = CapsuleHalfSegment(shapes[i]);
            // A sphere meets a capsule where its centre meets the capsule grown
            // by the sphere's radius.
            const std::optional<float> t = RayCapsule(from, direction, distance, centre, halfSegment,
                                                      shapes[i].Radius + definition.Definition.Radius);
            if (!t.has_value() || (nearest && *t >= nearest->Distance))
                continue;
            const Vec3d at = from + direction * *t;
            const float along = std::clamp((at - centre).Dot(Vec3d::Up()), -halfSegment, halfSegment);
            Vec3d normal = at - (centre + Vec3d::Up() * along);
            normal = normal.SqrMagnitude() > 1.0e-10f ? normal.Normalized() : -direction;
            nearest = Contact{ .Entity = entity,
                               .Distance = *t,
                               .Point = at - normal * definition.Definition.Radius,
                               .Normal = normal };
        }
    });
    return nearest;
}

bool ProjectileSystem::Strike(EntityId projectile, ProjectileMotion& motion, const ResolvedProjectile& definition,
                              EntityId instigator, const Contact& contact, const FixedSimTime& now)
{
    World& world = *Entities;
    Cue(instigator, projectile, definition.ImpactCue, contact.Point, contact.Normal, contact.Entity, now.TickIndex);
    // Level geometry with no entity of its own takes nothing and is told nothing.
    if (!contact.Entity.IsValid() || !world.IsAlive(contact.Entity) || Struck(motion, contact.Entity)
        || motion.HitCount >= kProjectileMaxHits)
        return false;
    motion.Hits[motion.HitCount++] = contact.Entity;
    ++StrikeCount;

    const EntityId target = contact.Entity;
    ApplyEffects(motion.Magnitude, definition, target, EffectId{}, now);
    if (Host.Responses != nullptr)
        (void)Host.Responses->Deliver(target, ProjectileHit{ .Instigator = instigator,
                                                            .Projectile = projectile,
                                                            .Kind = definition.HitKind,
                                                            .Point = contact.Point,
                                                            .Normal = contact.Normal });
    return true;
}

void ProjectileSystem::ApplyEffects(float magnitude, const ResolvedProjectile& definition, EntityId target,
                                    EffectId override, const FixedSimTime& now)
{
    World& world = *Entities;
    if (!world.IsAlive(target) || !world.HasComponent<AttributeSet>(target))
        return;
    if (override.IsValid())
    {
        (void)ApplyEffect(world, target, override, now, LeaseOwner{}, 1.0f);
        return;
    }
    if (definition.HitEffect.IsValid())
        (void)ApplyEffect(world, target, definition.HitEffect, now, LeaseOwner{}, 1.0f);
    if (definition.ScaledEffect.IsValid() && magnitude > 0.0f && world.IsAlive(target))
        (void)ApplyEffect(world, target, definition.ScaledEffect, now, LeaseOwner{}, magnitude);
}

void ProjectileSystem::Splash(ProjectileMotion& motion, const ResolvedProjectile& definition, EntityId instigator,
                              const Vec3d& point, const Vec3d& normal, const FixedSimTime& now)
{
    const float radius = definition.Definition.SplashRadius * motion.SplashScale;
    if (radius <= 0.0f || Host.Physics == nullptr)
        return;
    World& world = *Entities;
    std::array<EntityId, 1> self{ instigator };
    const PhysicsQueryFilter filter{ .IncludeTriggers = false,
                                     .IgnoreEntities = { self.data(), instigator.IsValid() ? 1u : 0u } };
    const PhysicsQueries queries(*Host.Physics);
    Overlaps.clear();
    queries.OverlapShape(CollisionShape::MakeSphere(radius), point, Quatf::Identity(), Overlaps, filter);

    // Characters have no body for the overlap to find.
    if (definition.Definition.HitsCharacters && world.IsRegistered<CharacterController>())
    {
        if (!Characters)
            Characters.emplace(world);
        Characters->ForEachChunk([&](auto& view) {
            auto shapes = view.template Read<CharacterController>();
            auto poses = view.template Read<WorldTransform>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                const EntityId entity = view.Entity(i);
                const float reach = radius + shapes[i].Radius;
                if (entity != instigator
                    && SegmentDistanceSq(point, poses[i].Value.Position, CapsuleHalfSegment(shapes[i]))
                           <= reach * reach)
                    Overlaps.push_back(entity);
            }
        });
    }
    // In identity order, so a splash over several targets applies the same way
    // on every run.
    std::sort(Overlaps.begin(), Overlaps.end());
    Overlaps.erase(std::unique(Overlaps.begin(), Overlaps.end()), Overlaps.end());

    for (const EntityId target : Overlaps)
    {
        if (!world.IsAlive(target) || Struck(motion, target) || motion.HitCount >= kProjectileMaxHits)
            continue;
        const WorldTransform* pose = std::as_const(world).TryGet<WorldTransform>(target);
        if (pose == nullptr)
            continue;
        if (definition.Definition.SplashNeedsSight)
        {
            const Vec3d from = point + normal * kSightLift;
            const Vec3d delta = pose->Value.Position - from;
            const float length = delta.Magnitude();
            if (length > 1.0e-3f)
            {
                const RaycastHit hit = queries.Raycast(from, delta / length, length, filter);
                if (hit.Hit && hit.Entity != target)
                    continue;
            }
        }
        motion.Hits[motion.HitCount++] = target;
        ApplyEffects(motion.Magnitude, definition, target, definition.SplashEffect, now);
    }
}

void ProjectileSystem::Cue(EntityId instigator, EntityId projectile, GameplayTagId cue, const Vec3d& point,
                           const Vec3d& normal, EntityId target, std::uint64_t tick)
{
    if (!cue.IsValid() || !instigator.IsValid())
        return;
    World& world = *Entities;
    AbilityCueLog* log = world.IsAlive(instigator) && world.IsRegistered<AbilityCueLog>()
        ? world.TryGet<AbilityCueLog>(instigator)
        : nullptr;
    if (log == nullptr)
        return;
    // Named by the projectile and the tick, which no ability activation shares.
    const std::uint64_t activation = 0x5052000000000000ull ^ (static_cast<std::uint64_t>(projectile.Index) << 24)
                                   ^ (tick & 0xFFFFFFull);
    (void)AppendAbilityCue(*log, AbilityCueEntry{ .Cue = cue,
                                                  .Activation = activation,
                                                  .Tick = tick,
                                                  .Point = point,
                                                  .Normal = normal,
                                                  .Target = target });
}

void ProjectileSystem::End(EntityId projectile, std::uint64_t tick)
{
    World& world = *Entities;
    if (ProjectileFlight* flight = world.TryGet<ProjectileFlight>(projectile))
        flight->EndTick = std::max<std::uint64_t>(tick, 1);
    Show(projectile, false);
    if (world.HasComponent<ProjectileMotion>(projectile))
        world.RemoveComponent<ProjectileMotion>(projectile);
}

void ProjectileSystem::Present(FixedLogicContext& ctx)
{
    // Without a shared clock there is no telling which authority tick this
    // machine is drawing, so mirrored projectiles are drawn as they arrive.
    std::optional<std::uint64_t> presented;
    if (Host.Clock != nullptr && Host.Interpolation != nullptr && Host.Clock->HasEstimate())
        presented = Host.Interpolation->PresentedAuthorityTick(*Host.Clock, ctx.Time.TickIndex);

    World& world = *Entities;
    if (!Flights)
        Flights.emplace(world);
    Scratch.clear();
    Flights->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        auto flights = view.template Write<ProjectileFlight>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const ProjectileFlight& flight = flights[i];
            const bool shown = presented.has_value()
                ? *presented >= flight.LaunchTick && (flight.EndTick == 0 || *presented <= flight.EndTick)
                : flight.EndTick == 0;
            if (shown != flight.Shown)
                Scratch.push_back(view.Entity(i));
        }
    });
    for (const EntityId projectile : Scratch)
        if (const ProjectileFlight* flight = std::as_const(world).TryGet<ProjectileFlight>(projectile))
        {
            // A mirrored projectile arrives with what replicates and what its
            // prefab holds; its pose comes each tick from interpolation, and is
            // drawn between ticks from here on. Snapped the first time, so it
            // does not streak from where it was built.
            const bool shown = !flight->Shown;
            if (!world.HasComponent<WorldTransformHistory>(projectile))
            {
                world.AddComponent<WorldTransformHistory>(projectile, WorldTransformHistory{});
                RequestTransformHistorySnap(world, projectile);
            }
            Show(projectile, shown);
        }
}

void ProjectileSystem::Show(EntityId projectile, bool shown)
{
    World& world = *Entities;
    ProjectileFlight* flight = world.TryGet<ProjectileFlight>(projectile);
    if (flight == nullptr)
        return;
    flight->Shown = shown;

    auto apply = [&](EntityId entity) {
        if (world.IsRegistered<StaticMeshComponent>())
            if (StaticMeshComponent* mesh = world.TryGet<StaticMeshComponent>(entity))
                mesh->Visible = shown;
        if (world.IsRegistered<SkinnedMeshComponent>())
            if (SkinnedMeshComponent* mesh = world.TryGet<SkinnedMeshComponent>(entity))
                mesh->Visible = shown;
    };
    const SceneInstance* group = world.IsRegistered<SceneInstance>()
        ? std::as_const(world).TryGet<SceneInstance>(projectile)
        : nullptr;
    const SceneInstanceIndex* index = world.TryGetResource<SceneInstanceIndex>();
    if (group != nullptr && index != nullptr && !index->Entities(group->Id).empty())
    {
        for (const EntityId member : index->Entities(group->Id))
            apply(member);
        return;
    }
    apply(projectile);
}

void ProjectileSystem::SetCapacity(std::size_t capacity)
{
    Entities->GetResource<ProjectileCount>().Capacity = capacity;
}

void ProjectileSystem::RegisterConsole(ConsoleRegistry& console)
{
    (void)console.RegisterCVar({
        .Name = "projectile.capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(kProjectileDefaultCapacity),
        .CurrentValue = static_cast<std::int64_t>(GetCapacity()),
        .Flags = CVarFlags::None,
        .Help = "How many projectiles may be in flight at once. A launch past it is refused.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    (void)console.RegisterCVar({
        .Name = "projectile.linger_ticks",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(LingerTicks),
        .CurrentValue = static_cast<std::int64_t>(LingerTicks),
        .Flags = CVarFlags::None,
        .Help = "Ticks an ended projectile is kept, hidden and inert, so that its end reaches every peer "
                "before it goes. Below the snapshot interval a short flight can end unseen.",
        .Source = { "engine" },
        .Min = 0.0,
        .Max = 600.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            SetLingerTicks(static_cast<std::uint32_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
}

ProjectileSystem& RegisterProjectileSystem(EngineSchedule& schedule, World& world,
                                           const ProjectileSystem::Services& services)
{
    return schedule.Register<ProjectileSystem>(world, services);
}

void ReplicationCodec<ProjectileFlight>::ToWire(const ReplicationWireContext& context, const ProjectileFlight& flight,
                                                ProjectileFlightWire& wire)
{
    wire = ProjectileFlightWire{ .Instigator = context.WireEntity(flight.Instigator),
                                 .LaunchTick = flight.LaunchTick,
                                 .EndTick = flight.EndTick };
}

void ReplicationCodec<ProjectileFlight>::FromWire(const ReplicationWireContext& context,
                                                  const ProjectileFlightWire& wire, ProjectileFlight& flight)
{
    flight.Instigator = context.LocalEntity(wire.Instigator);
    flight.LaunchTick = wire.LaunchTick;
    flight.EndTick = wire.EndTick;
}
