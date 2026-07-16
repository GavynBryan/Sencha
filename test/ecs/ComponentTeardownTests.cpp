#include <gtest/gtest.h>

#include <core/ResourceStore.h>
#include <ecs/CommandBuffer.h>
#include <ecs/Ecs.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

struct ComponentTeardownState
{
    int* AddCount = nullptr;
    int* RemoveCount = nullptr;
    int* LastValue = nullptr;
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
                      ResourceStore& resources,
                      EntityId)
    {
        ComponentTeardownState& state = resources.Get<ComponentTeardownState>();
        if (state.AddCount != nullptr)
            ++*state.AddCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
    }

    static void OnRemove(const ComponentTeardownProbe& component,
                         ResourceStore& resources,
                         EntityId)
    {
        ComponentTeardownState& state = resources.Get<ComponentTeardownState>();
        if (state.RemoveCount != nullptr)
            ++*state.RemoveCount;
        if (state.LastValue != nullptr)
            *state.LastValue = component.Value;
    }
};

namespace
{
    void PrepareWorld(World& world,
                      ResourceStore& resources,
                      int& removeCount,
                      int& lastValue,
                      int* addCount = nullptr)
    {
        resources.Register<ComponentTeardownState>(ComponentTeardownState{
            .AddCount = addCount,
            .RemoveCount = &removeCount,
            .LastValue = &lastValue,
        });
        world.RegisterComponent<ComponentTeardownProbe>();
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
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    const EntityId entity = AddProbe(world, 17);
    world.DestroyEntity(entity);

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 17);
    EXPECT_FALSE(world.IsAlive(entity));

    world.ClearEntities();
    EXPECT_EQ(removeCount, 1);
}

TEST(ComponentTeardown, CommandBufferDestroyRunsRemoveHook)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    const EntityId entity = AddProbe(world, 23);
    CommandBuffer commands(world);
    commands.DestroyEntity(entity);
    commands.Flush();

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 23);
    EXPECT_FALSE(world.IsAlive(entity));
}

TEST(ComponentTeardown, RawMutationUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue, &addCount);

    const EntityId entity = world.CreateEntity();
    const ComponentId component = world.GetComponentId<ComponentTeardownProbe>();
    const ComponentTeardownProbe value{ 29 };

    world.AddComponentRaw(entity, component, &value);
    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastValue, 29);

    world.RemoveComponentRaw(entity, component);
    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 29);
}

TEST(ComponentTeardown, CommandBufferUsesRegisteredLifecycleMetadata)
{
    int addCount = 0;
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue, &addCount);

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
}

TEST(ComponentTeardown, ClearEntitiesRunsEveryHookAndPreservesSetup)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    World world(resources);
    PrepareWorld(world, resources, removeCount, lastValue);

    AddProbe(world, 1);
    AddProbe(world, 2);
    AddProbe(world, 3);

    world.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(world.EntityCount(), 0u);
    EXPECT_TRUE(world.IsRegistered<ComponentTeardownProbe>());
    EXPECT_TRUE(resources.Has<ComponentTeardownState>());

    world.ClearEntities();
    EXPECT_EQ(removeCount, 3);
}

TEST(ComponentTeardown, WorldDestructorRunsHooksBeforeBoundResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    ResourceStore resources;
    {
        World world(resources);
        PrepareWorld(world, resources, removeCount, lastValue);
        AddProbe(world, 37);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 37);
    EXPECT_TRUE(resources.Has<ComponentTeardownState>());
}

TEST(ComponentTeardown, RegistryDestructorRunsHooksBeforeRegistryResourcesDie)
{
    int removeCount = 0;
    int lastValue = 0;
    {
        Registry registry(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });
        PrepareWorld(registry.Components, registry.Resources, removeCount, lastValue);
        AddProbe(registry.Components, 41);
    }

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 41);
}

TEST(ComponentTeardown, RegistriesUseIsolatedLifecycleResources)
{
    int leftRemoves = 0;
    int rightRemoves = 0;
    int leftValue = 0;
    int rightValue = 0;

    Registry left(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });
    Registry right(RegistryId{ 3, 1 }, RegistryKind::Zone, ZoneId{ 2 });
    PrepareWorld(left.Components, left.Resources, leftRemoves, leftValue);
    PrepareWorld(right.Components, right.Resources, rightRemoves, rightValue);

    const EntityId leftEntity = AddProbe(left.Components, 11);
    const EntityId rightEntity = AddProbe(right.Components, 22);

    left.Components.DestroyEntity(leftEntity);
    EXPECT_EQ(leftRemoves, 1);
    EXPECT_EQ(leftValue, 11);
    EXPECT_EQ(rightRemoves, 0);

    right.Components.DestroyEntity(rightEntity);
    EXPECT_EQ(rightRemoves, 1);
    EXPECT_EQ(rightValue, 22);
}

TEST(ComponentTeardown, DestroyZoneRunsHooks)
{
    int removeCount = 0;
    int lastValue = 0;
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(zone.Components, zone.Resources, removeCount, lastValue);
    AddProbe(zone.Components, 43);

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_EQ(removeCount, 1);
    EXPECT_EQ(lastValue, 43);
}

TEST(ComponentTeardown, ZoneRuntimeClearCoversGlobalActiveAndDormantRegistries)
{
    int removeCount = 0;
    int lastValue = 0;
    ZoneRuntime runtime;

    PrepareWorld(runtime.Global().Components, runtime.Global().Resources,
                 removeCount, lastValue);
    AddProbe(runtime.Global().Components, 1);

    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    PrepareWorld(active.Components, active.Resources, removeCount, lastValue);
    AddProbe(active.Components, 2);
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });

    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    PrepareWorld(dormant.Components, dormant.Resources, removeCount, lastValue);
    AddProbe(dormant.Components, 3);

    runtime.ClearEntities();

    EXPECT_EQ(removeCount, 3);
    EXPECT_EQ(runtime.Global().Components.EntityCount(), 0u);
    EXPECT_EQ(active.Components.EntityCount(), 0u);
    EXPECT_EQ(dormant.Components.EntityCount(), 0u);
}
