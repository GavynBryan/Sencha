#include <ecs/CommandBuffer.h>
#include <ecs/TransitionDispatch.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    // Per-component reaction context: on this component's on_add, log it and (if
    // `next` is set) defer adding `next` to the same entity into the active
    // reaction scope. That deferred add lands in the following wave.
    struct WaveCtx
    {
        ComponentId next = InvalidComponentId;
        std::vector<std::string>* log = nullptr;
    };

    void CascadeAdd(World& w, EntityId e, ComponentId id, const void*, void* ctx)
    {
        auto* c = static_cast<WaveCtx*>(ctx);
        c->log->push_back(std::to_string(id));
        if (c->next != InvalidComponentId)
        {
            if (CommandBuffer* rx = w.ActiveReactions())
            {
                const int zero = 0;
                rx->AddComponentRaw(e, c->next, &zero, sizeof(int), alignof(int));
            }
        }
    }

    // Ping-pong to force a non-converging cascade for the cycle-guard test.
    void PingRemove(World& w, EntityId e, ComponentId id, const void*, void*)
    {
        if (CommandBuffer* rx = w.ActiveReactions())
            rx->RemoveComponentRaw(e, id, sizeof(int));
    }
    void PongAdd(World& w, EntityId e, ComponentId id, const void*, void*)
    {
        if (CommandBuffer* rx = w.ActiveReactions())
        {
            const int zero = 0;
            rx->AddComponentRaw(e, id, &zero, sizeof(int), alignof(int));
        }
    }

    ComponentId MakeInt(World& w, const char* name, ComponentTypeId type)
    {
        return w.RegisterComponentRaw(name, type, sizeof(int), alignof(int), false);
    }
}

TEST(ReactionWave, ObserverDeferredEffectAppliesInNextWaveAndDrainsToQuiescence)
{
    World world;
    std::vector<std::string> log;
    const ComponentId a = MakeInt(world, "A", MakeComponentTypeId("test.wave_a"));
    const ComponentId b = MakeInt(world, "B", MakeComponentTypeId("test.wave_b"));
    const ComponentId c = MakeInt(world, "C", MakeComponentTypeId("test.wave_c"));

    WaveCtx ctxA{ b, &log };
    WaveCtx ctxB{ c, &log };
    WaveCtx ctxC{ InvalidComponentId, &log };
    world.RegisterTransitionDispatch(a, TransitionDispatch{ &CascadeAdd, nullptr, nullptr, &ctxA });
    world.RegisterTransitionDispatch(b, TransitionDispatch{ &CascadeAdd, nullptr, nullptr, &ctxB });
    world.RegisterTransitionDispatch(c, TransitionDispatch{ &CascadeAdd, nullptr, nullptr, &ctxC });

    const EntityId e = world.CreateEntity();
    CommandBuffer cmd(world);
    const int zero = 0;
    cmd.AddComponentRaw(e, a, &zero, sizeof(int), alignof(int));
    cmd.Flush();

    // Wave 0 adds A; on_add(A) defers add B (wave 1); on_add(B) defers add C
    // (wave 2); on_add(C) defers nothing. Breadth-first order A, B, C.
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], std::to_string(a));
    EXPECT_EQ(log[1], std::to_string(b));
    EXPECT_EQ(log[2], std::to_string(c));
    EXPECT_TRUE(world.HasComponent(e, a));
    EXPECT_TRUE(world.HasComponent(e, b));
    EXPECT_TRUE(world.HasComponent(e, c));
}

TEST(ReactionWave, ObservedAddsAreNotCoalescedSoEachFires)
{
    World world;
    std::vector<std::string> log;
    const ComponentId a = MakeInt(world, "A", MakeComponentTypeId("test.wave_a"));
    WaveCtx ctxA{ InvalidComponentId, &log };
    world.RegisterTransitionDispatch(a, TransitionDispatch{ &CascadeAdd, nullptr, nullptr, &ctxA });

    const EntityId e1 = world.CreateEntity();
    const EntityId e2 = world.CreateEntity();
    CommandBuffer cmd(world);
    const int zero = 0;
    // Two consecutive same-id hook-free adds: without the dispatch stamp these
    // would coalesce into a batch that fires no dispatch.
    cmd.AddComponentRaw(e1, a, &zero, sizeof(int), alignof(int));
    cmd.AddComponentRaw(e2, a, &zero, sizeof(int), alignof(int));
    cmd.Flush();

    EXPECT_EQ(log.size(), 2u); // both fired; the run was not coalesced away
    EXPECT_TRUE(world.HasComponent(e1, a));
    EXPECT_TRUE(world.HasComponent(e2, a));
}

TEST(ReactionWave, UnobservedAddsStillCoalesceAndApply)
{
    World world;
    const ComponentId u = MakeInt(world, "U", MakeComponentTypeId("test.wave_u"));
    // No dispatch registered for U: the consecutive run still batches, unchanged.
    const EntityId e1 = world.CreateEntity();
    const EntityId e2 = world.CreateEntity();
    CommandBuffer cmd(world);
    const int zero = 0;
    cmd.AddComponentRaw(e1, u, &zero, sizeof(int), alignof(int));
    cmd.AddComponentRaw(e2, u, &zero, sizeof(int), alignof(int));
    cmd.Flush();
    EXPECT_TRUE(world.HasComponent(e1, u));
    EXPECT_TRUE(world.HasComponent(e2, u));
}

TEST(ReactionWaveDeath, CycleGuardHardFailsOnNonConvergingCascade)
{
#ifdef NDEBUG
    GTEST_SKIP() << "Cycle guard asserts only in debug builds.";
#else
    World world;
    const ComponentId a = MakeInt(world, "A", MakeComponentTypeId("test.wave_a"));
    // on_add defers remove; on_remove defers add: the cascade never converges.
    world.RegisterTransitionDispatch(a, TransitionDispatch{ &PingRemove, &PongAdd, nullptr, nullptr });

    const EntityId e = world.CreateEntity();
    CommandBuffer cmd(world);
    const int zero = 0;
    cmd.AddComponentRaw(e, a, &zero, sizeof(int), alignof(int));
    EXPECT_DEATH(cmd.Flush(), "cycle guard");
#endif
}
