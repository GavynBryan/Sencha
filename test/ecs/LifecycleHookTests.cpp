// The ComponentTraits lifecycle-hook contract: a hooked component leaving the
// world fires OnRemove on every path — RemoveComponent, DestroyEntity, command
// buffer, and World teardown — and teardown fires hooks while World resources
// are still reachable, so retain/release components cannot leak their external
// handles on unload.

#include <ecs/CommandBuffer.h>
#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

// ─── Test components ─────────────────────────────────────────────────────────

static int              g_FirstAdds    = 0;
static EntityId         g_FirstAddEntity{};   // entity OnAdd was handed
static int              g_FirstRemoves = 0;
static int              g_SecondRemoves = 0;
static std::vector<int> g_RemoveOrder; // .Tag values in hook-fire order

struct HookFirst  { int Tag = 0; };
struct HookSecond { int Tag = 0; };
struct PlainData  { float Value = 0.f; };

template <>
struct ComponentTraits<HookFirst>
{
    static void OnAdd(HookFirst& /*c*/, World& /*w*/, EntityId e)
    {
        ++g_FirstAdds;
        g_FirstAddEntity = e;
    }
    static void OnRemove(const HookFirst& c, World& /*w*/, EntityId /*e*/)
    {
        ++g_FirstRemoves;
        g_RemoveOrder.push_back(c.Tag);
    }
};

template <>
struct ComponentTraits<HookSecond>
{
    static void OnRemove(const HookSecond& c, World& /*w*/, EntityId /*e*/)
    {
        ++g_SecondRemoves;
        g_RemoveOrder.push_back(c.Tag);
    }
};

// Retain/release against a World resource, the StaticMeshComponent pattern.
// MissedResource counts hook firings that could not reach the service — a
// nonzero count means teardown destroyed resources before draining hooks.
// RefReleases counts successful releases, so a teardown that fires no hooks
// at all cannot pass vacuously.
static int g_MissedResource = 0;
static int g_RefReleases = 0;

struct RefCountService
{
    std::array<int, 8> Counts{};
};

struct RefHandle { int Slot = 0; };

template <>
struct ComponentTraits<RefHandle>
{
    static void OnAdd(RefHandle& c, World& w, EntityId /*e*/)
    {
        if (auto* svc = w.TryGetResource<RefCountService>())
            ++svc->Counts[c.Slot];
        else
            ++g_MissedResource;
    }
    static void OnRemove(const RefHandle& c, World& w, EntityId /*e*/)
    {
        if (auto* svc = w.TryGetResource<RefCountService>())
        {
            --svc->Counts[c.Slot];
            ++g_RefReleases;
        }
        else
        {
            ++g_MissedResource;
        }
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(HookFirst,  "test.lifecycle.hook_first");
SENCHA_DECLARE_COMPONENT_TYPE(HookSecond, "test.lifecycle.hook_second");
SENCHA_DECLARE_COMPONENT_TYPE(PlainData,  "test.lifecycle.plain");
SENCHA_DECLARE_COMPONENT_TYPE(RefHandle,  "test.lifecycle.ref_handle");

// ─── Fixture ─────────────────────────────────────────────────────────────────

class LifecycleHookTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_FirstAdds     = 0;
        g_FirstAddEntity = EntityId{};
        g_FirstRemoves  = 0;
        g_SecondRemoves = 0;
        g_MissedResource = 0;
        g_RemoveOrder.clear();

        world.RegisterComponent<HookFirst>();
        world.RegisterComponent<HookSecond>();
        world.RegisterComponent<PlainData>();
        world.RegisterComponent<RefHandle>();
    }

    World world;
};

// ─── Entity destruction ──────────────────────────────────────────────────────

TEST_F(LifecycleHookTest, DestroyEntityFiresOnRemove)
{
    const EntityId e = world.CreateEntity();
    world.AddComponent<HookFirst>(e, { 7 });

    world.DestroyEntity(e);

    EXPECT_EQ(g_FirstRemoves, 1);
}

TEST_F(LifecycleHookTest, DestroyEntityFiresHookedColumnsInRegistrationOrder)
{
    const EntityId e = world.CreateEntity();
    // Added in reverse registration order; firing must follow registration
    // (column) order, not add order, so repeated runs are deterministic.
    world.AddComponent<HookSecond>(e, { 2 });
    world.AddComponent<HookFirst>(e, { 1 });

    world.DestroyEntity(e);

    ASSERT_EQ(g_RemoveOrder.size(), 2u);
    EXPECT_EQ(g_RemoveOrder[0], 1);
    EXPECT_EQ(g_RemoveOrder[1], 2);
}

TEST_F(LifecycleHookTest, DestroyEntityMidChunkFiresTheDestroyedRowsValue)
{
    // Swap-and-pop moves the last row into the vacated slot; the hook must see
    // the destroyed entity's component, not the moved neighbor's.
    const EntityId a = world.CreateEntity();
    const EntityId b = world.CreateEntity();
    const EntityId c = world.CreateEntity();
    world.AddComponent<HookFirst>(a, { 10 });
    world.AddComponent<HookFirst>(b, { 20 });
    world.AddComponent<HookFirst>(c, { 30 });
    g_RemoveOrder.clear();
    g_FirstRemoves = 0;

    world.DestroyEntity(b);

    ASSERT_EQ(g_FirstRemoves, 1);
    EXPECT_EQ(g_RemoveOrder[0], 20);
    EXPECT_TRUE(world.IsAlive(a));
    EXPECT_TRUE(world.IsAlive(c));
}

TEST_F(LifecycleHookTest, DestroyEntityWithoutHookedComponentsFiresNothing)
{
    const EntityId e = world.CreateEntity();
    world.AddComponent<PlainData>(e, { 1.f });

    world.DestroyEntity(e);

    EXPECT_EQ(g_FirstRemoves, 0);
    EXPECT_EQ(g_SecondRemoves, 0);
}

TEST_F(LifecycleHookTest, RemoveComponentThenDestroyFiresOnce)
{
    const EntityId e = world.CreateEntity();
    world.AddComponent<HookFirst>(e, { 5 });

    world.RemoveComponent<HookFirst>(e);
    EXPECT_EQ(g_FirstRemoves, 1);

    world.DestroyEntity(e);
    EXPECT_EQ(g_FirstRemoves, 1);
}

TEST_F(LifecycleHookTest, CommandBufferDestroyFiresOnRemoveAtFlush)
{
    const EntityId e = world.CreateEntity();
    world.AddComponent<HookFirst>(e, { 3 });

    CommandBuffer cmds(world);
    cmds.DestroyEntity(e);
    EXPECT_EQ(g_FirstRemoves, 0); // recorded, not yet executed

    cmds.Flush();

    EXPECT_EQ(g_FirstRemoves, 1);
    EXPECT_FALSE(world.IsAlive(e));
}

// A component added to an entity the buffer has not created yet still fires OnAdd,
// and the hook must be handed the entity that now exists. A hook is where external
// handles are retained against an id, so being given one that is not alive would
// leak or corrupt them.
TEST_F(LifecycleHookTest, CommandBufferAddToAPendingEntityFiresOnAddWithALiveEntity)
{
    CommandBuffer cmds(world);
    const PendingEntity spawned = cmds.CreateEntity();
    cmds.AddComponent<HookFirst>(spawned, { 9 });
    EXPECT_EQ(g_FirstAdds, 0); // recorded, not yet executed

    cmds.Flush();

    EXPECT_EQ(g_FirstAdds, 1);
    EXPECT_TRUE(world.IsAlive(g_FirstAddEntity))
        << "OnAdd was handed an entity that does not exist";

    const std::vector<EntityId> alive = world.GetAliveEntities();
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(g_FirstAddEntity, alive[0]);

    const HookFirst* component =
        std::as_const(world).TryGet<HookFirst>(g_FirstAddEntity);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->Tag, 9);
}

// ─── World teardown ──────────────────────────────────────────────────────────

TEST(LifecycleHookTeardown, WorldDestructionDrainsHooksBeforeResources)
{
    // The unload leak shape: components retain against a resource-owned
    // service on add and release on remove. Scope exit must release every
    // retain, and the service must still be alive when the hooks run.
    {
        World w;
        w.RegisterComponent<RefHandle>();
        auto& svc = w.AddResource<RefCountService>();
        g_MissedResource = 0;
        g_RefReleases = 0;

        const EntityId a = w.CreateEntity();
        const EntityId b = w.CreateEntity();
        w.AddComponent<RefHandle>(a, { 0 });
        w.AddComponent<RefHandle>(b, { 1 });
        EXPECT_EQ(svc.Counts[0], 1);
        EXPECT_EQ(svc.Counts[1], 1);

        const EntityId c = w.CreateEntity();
        w.AddComponent<RefHandle>(c, { 0 });
        EXPECT_EQ(svc.Counts[0], 2);

        // Sample on scope exit through the hooks themselves: if teardown
        // reaches the service, every count returns to zero and nothing is
        // recorded as missed.
    }

    EXPECT_EQ(g_RefReleases, 3)
        << "teardown did not fire OnRemove for every live component";
    EXPECT_EQ(g_MissedResource, 0)
        << "OnRemove ran after World resources were destroyed";
}

TEST(LifecycleHookTeardown, WorldDestructionFiresOneRemovePerLiveComponent)
{
    g_FirstRemoves  = 0;
    g_SecondRemoves = 0;
    {
        World w;
        w.RegisterComponent<HookFirst>();
        w.RegisterComponent<HookSecond>();

        const EntityId a = w.CreateEntity();
        w.AddComponent<HookFirst>(a, { 1 });
        const EntityId b = w.CreateEntity();
        w.AddComponent<HookFirst>(b, { 2 });
        w.AddComponent<HookSecond>(b, { 3 });

        // Destroyed-before-teardown entities must not fire again at teardown.
        const EntityId dead = w.CreateEntity();
        w.AddComponent<HookFirst>(dead, { 4 });
        w.DestroyEntity(dead);
        g_FirstRemoves = 0;
        g_SecondRemoves = 0;
    }

    EXPECT_EQ(g_FirstRemoves, 2);
    EXPECT_EQ(g_SecondRemoves, 1);
}

TEST(LifecycleHookTeardown, MoveAssignmentDrainsTargetHooks)
{
    g_FirstRemoves = 0;

    World target;
    target.RegisterComponent<HookFirst>();
    const EntityId e = target.CreateEntity();
    target.AddComponent<HookFirst>(e, { 9 });

    World source;
    target = std::move(source); // target's old state is torn down

    EXPECT_EQ(g_FirstRemoves, 1);
}
