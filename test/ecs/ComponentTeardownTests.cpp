#include <gtest/gtest.h>

#include <ecs/CommandBuffer.h>
#include <ecs/Ecs.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

struct ComponentTeardownState
{
    int* AddCount = nullptr;
    int* RemoveCount = nullptr;
    int* LastValue = nullptr;
    bool* EntityWasAlive = nullptr;
};

struct ComponentTeardownProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(ComponentTeardownProbe, "test.component_teardown_probe");

template <>
struct ComponentTraits<ComponentTeardownProbe>
{
    static void OnAdd(ComponentTeardownProbe& component,
                      World& world,
                      EntityId)
    {
        ComponentTeardownState& state = world.GetResource<ComponentTeardownState>();
        if (state.AddCount != nullptr)
            ++*state.AddCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
    }

    static void OnRemove(const ComponentTeardownProbe& component,
                         World& world,
                         EntityId entity)
    {
        ComponentTeardownState& state = world.GetResource<ComponentTeardownState>();
        if (state.RemoveCount != nullptr)
            ++*state.RemoveCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
        if (state.EntityWasAlive != nullptr)
            *state.EntityWasAlive = world.IsAlive(entity);
    }
};

namespace
{
    void PrepareWorld(World& world,
                      int& removeCount,
                      int& lastValue,
                      bool& entityWasAlive,
                      int* addCount = nullptr)
    {
        world.RegisterComponent<ComponentTeardownProbe>();
        world.AddResource<ComponentTeardownState>(ComponentTeardownState{
            .AddCount = addCount,
            .RemoveCount = &removeCount,
            .LastValue = &lastValue,
            .EntityWasAlive = &entityWasAlive,
        });
    }

    EntityId AddProbe(World& world, int value)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent(entity, ComponentTeardownProbe{ value });
        return entity;
    }
}

TEST(ComponentTeardown, DestroyEntityRunsRemoveHookExactlyOnce)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    World world;
    PrepareWorld(world, removeCount, lastValue, entityWasAlive);

    const EntityId entity = AddProbe(world, 17);
    world.DestroyEntity(entity);

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 17);
    EXPECT_TRUE(entityWasAlive);
    EXPECT_FALSE(world.IsAlive(entity));

    world.ClearEntities();
    EXPECT_EQ(removeCount, 1);
}

TEST(ComponentTeardown, CommandBufferDestroyRunsRemoveHook)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    World world;
    PrepareWorld(world, removeCount, lastValue, entityWasAlive);

    const EntityId entity = AddProbe(world, 23);
    CommandBuffer commands(world);
    commands.DestroyEntity(entity);
    commands.Flush();

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 23);
    EXPECT_TRUE(entityWasAlive);
    EXPECT_FALSE(world.IsAlive(entity));
}

TEST(ComponentTeardown, RawMutationUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    World world;
    PrepareWorld(world, removeCount, lastValue, entityWasAlive, &addCount);

    const EntityId entity = world.CreateEntity();
    const ComponentId component = world.GetComponentId<ComponentTeardownProbe>();
    const ComponentTeardownProbe value{ 29 };

    world.AddComponentRaw(entity, component, &value);
    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastValue, 29);

    world.RemoveComponentRaw(entity, component);
    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 29);
    EXPECT_TRUE(entityWasAlive);
}

TEST(ComponentTeardown, CommandBufferUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    World world;
    PrepareWorld(world, removeCount, lastValue, entityWasAlive, &addCount);

    const EntityId entity = world.CreateEntity();
    CommandBuffer commands(world);
    commands.AddComponent(entity, ComponentTeardownProbe{ 31 });
    commands.Flush();

    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastValue, 31);

    commands.RemoveComponent<ComponentTeardownProbe>(entity);
    commands.Flush();

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 31);
    EXPECT_TRUE(entityWasAlive);
}

TEST(ComponentTeardown, ClearEntitiesRunsEveryHookAndPreservesWorldSetup)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    World world;
    PrepareWorld(world, removeCount, lastValue, entityWasAlive);

    AddProbe(world, 1);
    AddProbe(world, 2);
    AddProbe(world, 3);

    world.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(world.EntityCount(), 0u);
    EXPECT_TRUE(world.IsRegistered<ComponentTeardownProbe>());
    EXPECT_TRUE(world.HasResource<ComponentTeardownState>());

    world.ClearEntities();
    EXPECT_EQ(removeCount, 3);
}

TEST(ComponentTeardown, WorldDestructorRunsHooksBeforeWorldResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    {
        World world;
        PrepareWorld(world, removeCount, lastValue, entityWasAlive);
        AddProbe(world, 37);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 37);
    EXPECT_TRUE(entityWasAlive);
}

TEST(ComponentTeardown, RegistryDestructorRunsHooksBeforeRegistryResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    {
        Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
        PrepareWorld(registry.Components, removeCount, lastValue, entityWasAlive);
        AddProbe(registry.Components, 41);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 41);
    EXPECT_TRUE(entityWasAlive);
}

TEST(ComponentTeardown, DestroyZoneRunsHooks)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(zone.Components, removeCount, lastValue, entityWasAlive);
    AddProbe(zone.Components, 43);

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 43);
    EXPECT_TRUE(entityWasAlive);
}

TEST(ComponentTeardown, ZoneRuntimeClearCoversGlobalActiveAndDormantRegistries)
{
    int removeCount = 0;
    int lastValue = 0;
    bool entityWasAlive = false;
    ZoneRuntime runtime;

    PrepareWorld(runtime.Global().Components, removeCount, lastValue, entityWasAlive);
    AddProbe(runtime.Global().Components, 1);

    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(active.Components, removeCount, lastValue, entityWasAlive);
    AddProbe(active.Components, 2);
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });

    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    PrepareWorld(dormant.Components, removeCount, lastValue, entityWasAlive);
    AddProbe(dormant.Components, 3);

    runtime.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(runtime.Global().Components.EntityCount(), 0u);
    EXPECT_EQ(active.Components.EntityCount(), 0u);
    EXPECT_EQ(dormant.Components.EntityCount(), 0u);
}
