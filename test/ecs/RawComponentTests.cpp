#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <bit>
#include <cstring>
#include <set>
#include <vector>

namespace
{
    // A stand-in for a script-defined component: three f32 leaves the runtime
    // knows only by ComponentTypeId, offset, and size.
    constexpr ComponentTypeId kBobMotion = MakeComponentTypeId("script.BobMotion");

    struct BobBytes
    {
        float BaseHeight;
        float Amplitude;
        float Frequency;
    };
}

TEST(RegisterComponentRaw, RegistersAndStoresByType)
{
    World world;
    const ComponentId id = world.RegisterComponentRaw("BobMotion", kBobMotion,
                                                       sizeof(BobBytes), alignof(BobBytes), false);
    EXPECT_NE(id, InvalidComponentId);
    EXPECT_EQ(world.GetComponentIdByType(kBobMotion), id);
    EXPECT_TRUE(world.IsRegistered(kBobMotion));

    const ComponentMeta* meta = world.GetMeta(id);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->Name, "BobMotion");
    EXPECT_EQ(meta->Size, sizeof(BobBytes));
    EXPECT_FALSE(meta->IsTag);

    const EntityId entity = world.CreateEntity();
    const BobBytes value{ 100.0f, 12.0f, 0.8f };
    world.AddComponentRaw(entity, id, &value, sizeof(value), alignof(BobBytes), nullptr);

    void* raw = world.GetComponentRaw(entity, id);
    ASSERT_NE(raw, nullptr);
    BobBytes readBack{};
    std::memcpy(&readBack, raw, sizeof(readBack));
    EXPECT_EQ(readBack.BaseHeight, 100.0f);
    EXPECT_EQ(readBack.Amplitude, 12.0f);

    // Reflected write through the raw pointer round-trips.
    const float newHeight = 137.0f;
    std::memcpy(static_cast<std::byte*>(raw) + offsetof(BobBytes, BaseHeight),
                &newHeight, sizeof(float));
    std::memcpy(&readBack, world.GetComponentRaw(entity, id), sizeof(readBack));
    EXPECT_EQ(readBack.BaseHeight, 137.0f);
}

TEST(RegisterComponentRaw, DedupsByTypeId)
{
    World world;
    const ComponentId a = world.RegisterComponentRaw("Foo", MakeComponentTypeId("script.Foo"),
                                                      8, 8, false);
    const ComponentId b = world.RegisterComponentRaw("Foo", MakeComponentTypeId("script.Foo"),
                                                      8, 8, false);
    EXPECT_EQ(a, b);
}

TEST(RegisterComponentRaw, RegistersTagComponent)
{
    World world;
    const ComponentId id = world.RegisterComponentRaw("Marker",
                                                      MakeComponentTypeId("script.Marker"),
                                                      0, 1, true);
    const ComponentMeta* meta = world.GetMeta(id);
    ASSERT_NE(meta, nullptr);
    EXPECT_TRUE(meta->IsTag);
    EXPECT_EQ(meta->Size, 0u);

    const EntityId entity = world.CreateEntity();
    world.AddComponentRaw(entity, id, nullptr, 0, 1, nullptr);
    EXPECT_TRUE(world.HasComponent(entity, id));
}

TEST(RegisterComponentRaw, NameStorageOutlivesTemporary)
{
    World world;
    ComponentId id;
    {
        std::string temporary = "Transient";
        id = world.RegisterComponentRaw(temporary, MakeComponentTypeId("script.Transient"),
                                        4, 4, false);
        temporary = "clobbered";
    }
    // The World copied the name, so the meta view is still valid.
    EXPECT_EQ(world.GetMeta(id)->Name, "Transient");
}

TEST(CommandBufferRaw, AddAndRemoveDeferred)
{
    World world;
    const ComponentId id = world.RegisterComponentRaw("Data", MakeComponentTypeId("script.Data"),
                                                       sizeof(int32_t), alignof(int32_t), false);
    const EntityId entity = world.CreateEntity();

    CommandBuffer cmd(world);
    const int32_t payload = 42;
    cmd.AddComponentRaw(entity, id, &payload, sizeof(payload), alignof(int32_t));
    // Not applied until flush.
    EXPECT_FALSE(world.HasComponent(entity, id));
    cmd.Flush();

    ASSERT_TRUE(world.HasComponent(entity, id));
    int32_t stored = 0;
    std::memcpy(&stored, world.GetComponentRaw(entity, id), sizeof(stored));
    EXPECT_EQ(stored, 42);

    CommandBuffer removal(world);
    removal.RemoveComponentRaw(entity, id, sizeof(int32_t));
    removal.Flush();
    EXPECT_FALSE(world.HasComponent(entity, id));
}

TEST(CommandBufferRaw, RawAddMatchesTemplatedPayloadCapture)
{
    // The raw payload is copied into the arena at record time, so mutating
    // the source afterward does not change the stored value.
    World world;
    const ComponentId id = world.RegisterComponentRaw("Blob", MakeComponentTypeId("script.Blob"),
                                                       sizeof(int32_t), alignof(int32_t), false);
    const EntityId entity = world.CreateEntity();

    CommandBuffer cmd(world);
    int32_t payload = 7;
    cmd.AddComponentRaw(entity, id, &payload, sizeof(payload), alignof(int32_t));
    payload = 999; // must not affect the recorded command
    cmd.Flush();

    int32_t stored = 0;
    std::memcpy(&stored, world.GetComponentRaw(entity, id), sizeof(stored));
    EXPECT_EQ(stored, 7);
}

TEST(ForEachEntityMatching, YieldsOnlyEntitiesWithAllRequiredComponents)
{
    // The required-signature iterator (the runtime half of a `requires` behavior
    // tick) visits exactly the entities whose archetype has every required id.
    World world;
    const ComponentId a = world.RegisterComponentRaw("A", MakeComponentTypeId("script.A"),
                                                      sizeof(int32_t), alignof(int32_t), false);
    const ComponentId b = world.RegisterComponentRaw("B", MakeComponentTypeId("script.B"),
                                                      sizeof(int32_t), alignof(int32_t), false);

    auto make = [&](bool hasA, bool hasB) {
        const EntityId e = world.CreateEntity();
        const int32_t v = 0;
        if (hasA)
            world.AddComponentRaw(e, a, &v, sizeof(v), alignof(int32_t), nullptr);
        if (hasB)
            world.AddComponentRaw(e, b, &v, sizeof(v), alignof(int32_t), nullptr);
        return e;
    };
    const EntityId ab1 = make(true, true);
    const EntityId ab2 = make(true, true);
    make(true, false);  // A only
    make(false, true);  // B only
    make(false, false); // neither

    auto collect = [&](std::vector<ComponentId> required) {
        std::set<std::uint32_t> seen;
        world.ForEachEntityMatching(required, [&](EntityId e) { seen.insert(e.Index); });
        return seen;
    };

    // Requiring {A, B} yields exactly the two entities that have both.
    const std::set<std::uint32_t> both = collect({ a, b });
    EXPECT_EQ(both.size(), 2u);
    EXPECT_TRUE(both.count(ab1.Index));
    EXPECT_TRUE(both.count(ab2.Index));

    // Requiring {A} yields the three A-bearing entities (two AB + one A-only).
    EXPECT_EQ(collect({ a }).size(), 3u);
    // Requiring {B} yields the three B-bearing entities (two AB + one B-only).
    EXPECT_EQ(collect({ b }).size(), 3u);
}
