#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>

#include <gtest/gtest.h>

struct RuntimeShutdownTracked
{
    int Value = 0;
};

struct RuntimeShutdownHookResource
{
    int* Calls = nullptr;
};

struct RuntimeShutdownZoneResource
{
    explicit RuntimeShutdownZoneResource(int* destructions)
        : Destructions(destructions)
    {
    }

    ~RuntimeShutdownZoneResource()
    {
        if (Destructions != nullptr)
            ++*Destructions;
    }

    int* Destructions = nullptr;
};

SENCHA_DECLARE_COMPONENT_TYPE(
    RuntimeShutdownTracked,
    "test.runtime_shutdown_tracked");

template <>
struct ComponentTraits<RuntimeShutdownTracked>
{
    static void OnRemove(
        const RuntimeShutdownTracked&,
        World& world,
        EntityId)
    {
        RuntimeShutdownHookResource& resource =
            world.GetResource<RuntimeShutdownHookResource>();
        ASSERT_NE(resource.Calls, nullptr);
        ++*resource.Calls;
    }
};

TEST(UnifiedRuntimeWorld, ShutdownDrainsZoneEntitiesBeforeResources)
{
    int hookCalls = 0;
    int zoneResourceDestructions = 0;

    {
        WorldComponentSchema schema;
        schema.Add<RuntimeShutdownTracked>();
        schema.Seal();

        RuntimeWorld runtime(schema);
        runtime.Entities().AddResource<RuntimeShutdownHookResource>(
            RuntimeShutdownHookResource{ &hookCalls });

        RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 });
        zone.Resources.Register<RuntimeShutdownZoneResource>(
            &zoneResourceDestructions);

        const EntityId entity =
            runtime.Entities().CreateEntity(zone.Partition);
        runtime.Entities().AddComponent<RuntimeShutdownTracked>(
            entity,
            RuntimeShutdownTracked{ 1 });
    }

    EXPECT_EQ(hookCalls, 1);
    EXPECT_EQ(zoneResourceDestructions, 1);
}
