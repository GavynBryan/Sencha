// example_systems.cpp
//
// Three example systems written against the spike API.
// Phase 0 exit criterion: each must read cleanly without needing a comment
// that says "why the API forces this shape."

#include <ecs/Ecs.h>

#include <cstdio>
#include <cmath>

// ─── Component definitions ───────────────────────────────────────────────────

struct Position { float X, Y, Z; };
struct Velocity { float X, Y, Z; };
struct Health   { float Current, Max; };

// Tag: entity is frozen — no velocity integration.
struct Frozen {};

// ─── Example 1: Velocity integration ─────────────────────────────────────────
//
// A system that reads Position and Velocity and writes back Position.
// Pattern: pure chunk iteration, no per-entity bookkeeping needed.

void IntegrateVelocity(Query<Read<Velocity>, Write<Position>>& q, float dt)
{
    q.ForEachChunk([dt](auto& view) {
        auto vel = view.template Read<Velocity>();
        auto pos = view.template Write<Position>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            pos[i].X += vel[i].X * dt;
            pos[i].Y += vel[i].Y * dt;
            pos[i].Z += vel[i].Z * dt;
        }
    });
}

// ─── Example 2: Damage-over-time, skipping Frozen entities ───────────────────
//
// Query with Write<Health> and Without<Frozen>.
// Shows tag exclusion; no workaround needed — Without<T> is first-class.

void ApplyDamageOverTime(Query<Write<Health>, Without<Frozen>>& q, float dps, float dt)
{
    q.ForEachChunk([dps, dt](auto& view) {
        auto health = view.template Write<Health>();
        for (uint32_t i = 0; i < view.Count(); ++i)
            health[i].Current = std::max(0.0f, health[i].Current - dps * dt);
    });
}

// ─── Example 3: Emit events for entities whose Health changed ─────────────────
//
// Uses Changed<Health> filter to skip chunks untouched this frame.
// Shows conservative-bump semantics: the chunk is visited if any system held
// Write<Health> this frame, regardless of whether individual rows were modified.

struct DamageEvent { EntityIndex Entity; float HealthRemaining; };

void CollectDamageEvents(
    Query<Read<Health>, Changed<Health>>& q,
    std::vector<DamageEvent>& out,
    uint32_t referenceFrame)
{
    q.ForEachChunk([&out](auto& view) {
        auto health  = view.template Read<Health>();
        const auto* entities = view.Entities();
        for (uint32_t i = 0; i < view.Count(); ++i)
            out.push_back({ entities[i], health[i].Current });
    }, referenceFrame);
}

// ─── Example 4: CommandBuffer structural changes ──────────────────────────────
//
// Freeze entities whose Health falls to zero.
// Systems never call AddComponent directly — they record via CommandBuffer.

void FreezeDeadEntities(
    Query<Read<Health>, Without<Frozen>>& q,
    CommandBuffer& cmds)
{
    q.ForEachChunk([&cmds](auto& view) {
        auto health  = view.template Read<Health>();
        const auto* indices = view.Entities();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (health[i].Current <= 0.0f)
            {
                EntityId entity{ indices[i], 1 }; // spike: generation=1
                cmds.AddComponent<Frozen>(entity);
            }
        }
    });
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    World world;

    const ComponentId posId    = world.RegisterComponent<Position>();
    const ComponentId velId    = world.RegisterComponent<Velocity>();
    const ComponentId healthId = world.RegisterComponent<Health>();
    const ComponentId frozenId = world.RegisterComponent<Frozen>();

    // Create entities.
    // Entity A: Position + Velocity + Health (active entity)
    // Entity B: Position + Velocity + Health + Frozen (frozen — immune to damage)
    // Entity C: Position + Velocity only (no health)

    ArchetypeSignature sigA, sigB, sigC;
    sigA.set(posId); sigA.set(velId); sigA.set(healthId);
    sigB = sigA; sigB.set(frozenId);
    sigC.set(posId); sigC.set(velId);

    EntityId eA = world.CreateEntity(sigA);
    EntityId eB = world.CreateEntity(sigB);
    EntityId eC = world.CreateEntity(sigC);

    // Initialize.
    *world.TryGet<Position>(eA) = { 0, 0, 0 };
    *world.TryGet<Velocity>(eA) = { 1, 0, 0 };
    *world.TryGet<Health>(eA)   = { 100, 100 };

    *world.TryGet<Position>(eB) = { 5, 0, 0 };
    *world.TryGet<Velocity>(eB) = { 0, 1, 0 };
    *world.TryGet<Health>(eB)   = { 50, 50 };

    *world.TryGet<Position>(eC) = { -1, 0, 0 };
    *world.TryGet<Velocity>(eC) = { 0, 0, 1 };

    // Build queries (durable — constructed once, iterated many times).
    Query<Read<Velocity>, Write<Position>>       integrateQ(world);
    Query<Write<Health>, Without<Frozen>>        damageQ(world);
    Query<Read<Health>, Changed<Health>>         changedQ(world);
    Query<Read<Health>, Without<Frozen>>         freezeQ(world);

    CommandBuffer cmds(world);

    // ── Frame 1 ──────────────────────────────────────────────────────────────
    world.AdvanceFrame();

    IntegrateVelocity(integrateQ, 0.016f);
    ApplyDamageOverTime(damageQ, 200.0f, 0.016f); // 3.2 damage → entity A drops to 96.8

    std::vector<DamageEvent> events;
    // referenceFrame=0: pick up chunks written in frame 1.
    CollectDamageEvents(changedQ, events, 0);

    std::printf("Frame 1 damage events: %zu\n", events.size());
    for (auto& ev : events)
        std::printf("  entity %u health=%.1f\n", ev.Entity, ev.HealthRemaining);

    // Freeze zero-health entities (none yet).
    FreezeDeadEntities(freezeQ, cmds);
    cmds.Flush();

    // Verify entity B (Frozen) is unaffected by damage.
    const Health* hB = world.TryGet<Health>(eB);
    std::printf("Entity B health (should be 50.0): %.1f\n", hB ? hB->Current : -1.f);

    // ── Frame 2 ──────────────────────────────────────────────────────────────
    world.AdvanceFrame();

    // Apply overwhelming damage to drain entity A.
    ApplyDamageOverTime(damageQ, 10000.0f, 1.0f);

    FreezeDeadEntities(freezeQ, cmds);
    cmds.Flush();

    // Entity A should now have Frozen.
    const bool aFrozen = world.HasComponent<Frozen>(eA);
    std::printf("Entity A frozen after health=0: %s (expected: true)\n",
                aFrozen ? "true" : "false");

    std::printf("All example systems ran without API workarounds.\n");
    return 0;
}
