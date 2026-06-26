#include <ecs/World.h>

#include <gtest/gtest.h>

// Components identified purely by an explicit stable name — no typeid anywhere.
struct StableA { int X = 0; };
struct StableB { float Y = 0.f; };
struct StableTag {};
SENCHA_DECLARE_COMPONENT_TYPE(StableA,   "test.stable_a");
SENCHA_DECLARE_COMPONENT_TYPE(StableB,   "test.stable_b");
SENCHA_DECLARE_COMPONENT_TYPE(StableTag, "test.stable_tag");

// A distinct C++ type that lies about sharing StableA's stable name with a
// different storage layout — the cross-module aliasing case the World must catch.
struct StableAImpostor { double A = 0, B = 0, C = 0, D = 0; };
SENCHA_DECLARE_COMPONENT_TYPE(StableAImpostor, "test.stable_a");

TEST(WorldStableIdentity, RegisterLookupAddByStableName)
{
    World w;
    w.RegisterComponent<StableA>();
    w.RegisterComponent<StableB>();

    const EntityId e = w.CreateEntity();
    w.AddComponent<StableA>(e, { 42 });

    ASSERT_NE(w.TryGet<StableA>(e), nullptr);
    EXPECT_EQ(w.TryGet<StableA>(e)->X, 42);
    EXPECT_TRUE(w.HasComponent<StableA>(e));
    EXPECT_FALSE(w.HasComponent<StableB>(e));
}

TEST(WorldStableIdentity, MetaCarriesStableIdentity)
{
    World w;
    const ComponentId id = w.RegisterComponent<StableA>();
    const ComponentMeta* meta = w.GetMeta(id);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->TypeId, MakeComponentTypeId("test.stable_a"));
    EXPECT_EQ(meta->Name, "test.stable_a");
}

TEST(WorldStableIdentity, ArchetypeMovePreservesDataAcrossStableIds)
{
    World w;
    w.RegisterComponent<StableA>();
    w.RegisterComponent<StableB>();

    const EntityId e = w.CreateEntity();
    w.AddComponent<StableA>(e, { 7 });
    w.AddComponent<StableB>(e, { 1.5f }); // moves the entity to a new archetype

    ASSERT_NE(w.TryGet<StableA>(e), nullptr);
    ASSERT_NE(w.TryGet<StableB>(e), nullptr);
    EXPECT_EQ(w.TryGet<StableA>(e)->X, 7);
    EXPECT_FLOAT_EQ(w.TryGet<StableB>(e)->Y, 1.5f);
}

TEST(WorldStableIdentity, TagComponentResolvesAndStores)
{
    World w;
    w.RegisterComponent<StableTag>();
    const ComponentMeta* meta = w.GetMeta(w.GetComponentId<StableTag>());
    ASSERT_NE(meta, nullptr);
    EXPECT_TRUE(meta->IsTag);

    const EntityId e = w.CreateEntity();
    w.AddComponent<StableTag>(e);
    EXPECT_TRUE(w.HasComponent<StableTag>(e));
}

TEST(WorldStableIdentity, StorageContractConflictFires)
{
#ifdef NDEBUG
    GTEST_SKIP() << "Conflict assertion only fires in debug builds.";
#else
    EXPECT_DEATH(
        {
            World w;
            w.RegisterComponent<StableA>();         // 4-byte layout
            w.RegisterComponent<StableAImpostor>(); // same stable name, 32-byte layout
        },
        "different storage layout");
#endif
}
