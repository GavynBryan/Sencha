#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

// ─── Test components ─────────────────────────────────────────────────────────

struct Pos   { float X = 0, Y = 0, Z = 0; };
struct Vel   { float X = 0, Y = 0, Z = 0; };
struct HP    { float Value = 100.f; };
struct Mass  { float Value = 1.f; };

// Tag (zero-size)
struct TagFrozen  {};
struct TagPlayer  {};

// Component with lifecycle hooks — tracks OnAdd / OnRemove call counts.
static int g_OnAddCount    = 0;
static int g_OnRemoveCount = 0;

struct Tracked { int Id = 0; };
struct HookAddsMass { int Id = 0; };

template <>
struct ComponentTraits<Tracked>
{
    static void OnAdd(Tracked& /*c*/, World& /*w*/, EntityId /*e*/)
    {
        ++g_OnAddCount;
    }
    static void OnRemove(const Tracked& /*c*/, World& /*w*/, EntityId /*e*/)
    {
        ++g_OnRemoveCount;
    }
};

template <>
struct ComponentTraits<HookAddsMass>
{
    static void OnAdd(HookAddsMass& /*c*/, World& w, EntityId e)
    {
        w.AddComponent<Mass>(e, { 10.f });
    }
};

// ─── Fixture: fresh world with standard components registered ─────────────────

class EcsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_OnAddCount    = 0;
        g_OnRemoveCount = 0;

        world.RegisterComponent<Pos>();
        world.RegisterComponent<Vel>();
        world.RegisterComponent<HP>();
        world.RegisterComponent<Mass>();
        world.RegisterComponent<TagFrozen>();
        world.RegisterComponent<TagPlayer>();
        world.RegisterComponent<Tracked>();
        world.RegisterComponent<HookAddsMass>();
    }

    World world;
};

// ─── Component registration ──────────────────────────────────────────────────

TEST_F(EcsTest, RegisteredComponentsHaveStableIds)
{
    const ComponentId posId = world.GetComponentId<Pos>();
    EXPECT_NE(posId, InvalidComponentId);
    EXPECT_EQ(world.GetComponentId<Pos>(), posId); // stable
}

TEST_F(EcsTest, DoubleRegistrationReturnsSameId)
{
    // Registering the same type twice must return the same id.
    // We need a fresh world that hasn't had any entities yet.
    World w2;
    const ComponentId a = w2.RegisterComponent<Pos>();
    const ComponentId b = w2.RegisterComponent<Pos>();
    EXPECT_EQ(a, b);
}

TEST_F(EcsTest, RegistrationAfterEntityCreationAssertsInDebug)
{
    world.CreateEntity();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.RegisterComponent<float>(),
                 "Component registration after entity creation");
#endif
}

TEST_F(EcsTest, TagComponentIsRegisteredCorrectly)
{
    EXPECT_TRUE(world.IsRegistered<TagFrozen>());
    const ComponentMeta* meta = world.GetMeta(world.GetComponentId<TagFrozen>());
    ASSERT_NE(meta, nullptr);
    EXPECT_TRUE(meta->IsTag);
    EXPECT_EQ(meta->Size, 0u);
}

// ─── Entity create / destroy ─────────────────────────────────────────────────

TEST_F(EcsTest, CreateEntityReturnsValidId)
{
    EntityId e = world.CreateEntity();
    EXPECT_TRUE(e.IsValid());
    EXPECT_TRUE(world.IsAlive(e));
}

TEST_F(EcsTest, DestroyEntityMakesItDead)
{
    EntityId e = world.CreateEntity();
    world.DestroyEntity(e);
    EXPECT_FALSE(world.IsAlive(e));
}

TEST_F(EcsTest, RecycledIndexHasDifferentGeneration)
{
    EntityId first = world.CreateEntity();
    const EntityIndex idx = first.Index;
    world.DestroyEntity(first);

    EntityId second = world.CreateEntity();
    EXPECT_EQ(second.Index, idx);
    EXPECT_NE(second.Generation, first.Generation);
    EXPECT_FALSE(world.IsAlive(first));
    EXPECT_TRUE(world.IsAlive(second));
}

TEST_F(EcsTest, EntityCountTracksLiveEntities)
{
    EXPECT_EQ(world.EntityCount(), 0u);
    EntityId e1 = world.CreateEntity();
    EntityId e2 = world.CreateEntity();
    EXPECT_EQ(world.EntityCount(), 2u);
    world.DestroyEntity(e1);
    EXPECT_EQ(world.EntityCount(), 1u);
    world.DestroyEntity(e2);
    EXPECT_EQ(world.EntityCount(), 0u);
}

TEST_F(EcsTest, DestroyDuringActiveQueryAssertsInDebug)
{
    EntityId e = world.CreateEntity();
    world.PushQueryScope();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.DestroyEntity(e), "DestroyEntity called while a query/lifecycle hook is active");
#endif
    world.PopQueryScope();
}

// ─── AddComponent / RemoveComponent ─────────────────────────────────────────

TEST_F(EcsTest, AddComponentMakesItVisible)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 1.f, 2.f, 3.f });
    EXPECT_TRUE(world.HasComponent<Pos>(e));
    const Pos* p = world.TryGet<Pos>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->X, 1.f);
    EXPECT_EQ(p->Y, 2.f);
    EXPECT_EQ(p->Z, 3.f);
}

TEST_F(EcsTest, RemoveComponentMakesItInvisible)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    world.RemoveComponent<Pos>(e);
    EXPECT_FALSE(world.HasComponent<Pos>(e));
    EXPECT_EQ(world.TryGet<Pos>(e), nullptr);
}

TEST_F(EcsTest, AddMultipleComponentsIndependently)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    world.AddComponent<Vel>(e, { 0.f, 1.f, 0.f });
    world.AddComponent<HP>(e, { 50.f });

    EXPECT_TRUE(world.HasComponent<Pos>(e));
    EXPECT_TRUE(world.HasComponent<Vel>(e));
    EXPECT_TRUE(world.HasComponent<HP>(e));
    EXPECT_EQ(world.TryGet<Pos>(e)->X, 1.f);
    EXPECT_EQ(world.TryGet<Vel>(e)->Y, 1.f);
    EXPECT_EQ(world.TryGet<HP>(e)->Value, 50.f);
}

TEST_F(EcsTest, RemoveOneOfMultipleComponentsPreservesOthers)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 3.f, 0.f, 0.f });
    world.AddComponent<Vel>(e, { 0.f, 0.f, 2.f });
    world.RemoveComponent<Pos>(e);

    EXPECT_FALSE(world.HasComponent<Pos>(e));
    EXPECT_TRUE(world.HasComponent<Vel>(e));
    EXPECT_EQ(world.TryGet<Vel>(e)->Z, 2.f);
}

TEST_F(EcsTest, TryGetReturnsNullForDeadEntity)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, {});
    world.DestroyEntity(e);
    EXPECT_EQ(world.TryGet<Pos>(e), nullptr);
}

TEST_F(EcsTest, AddComponentDuringActiveQueryAssertsInDebug)
{
    EntityId e = world.CreateEntity();
    world.PushQueryScope();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.AddComponent<Pos>(e), "AddComponent called while a query/lifecycle hook is active");
#endif
    world.PopQueryScope();
}

// ─── Tag components ──────────────────────────────────────────────────────────

TEST_F(EcsTest, TagComponentHasNoDataColumn)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<TagFrozen>(e);
    EXPECT_TRUE(world.HasComponent<TagFrozen>(e));

    ArchetypeSignature tagSig;
    tagSig.set(world.GetComponentId<TagFrozen>());

    bool foundTagArchetype = false;
    for (const auto& arch : world.GetArchetypes())
    {
        if (arch->Signature == tagSig)
        {
            foundTagArchetype = true;
            EXPECT_TRUE(arch->Columns.empty());
            ASSERT_FALSE(arch->Chunks.empty());
            EXPECT_EQ(arch->Chunks[0]->ColumnCount, 0u);
        }
    }
    EXPECT_TRUE(foundTagArchetype);
}

TEST_F(EcsTest, TagComponentsDoNotAffectRowSize)
{
    // Two entities with only a tag should fit in many rows per chunk.
    for (int i = 0; i < 100; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<TagFrozen>(e);
    }
    EXPECT_EQ(world.EntityCount(), 100u);
}

TEST_F(EcsTest, TagComponentRemoval)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<TagFrozen>(e);
    world.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    world.RemoveComponent<TagFrozen>(e);
    EXPECT_FALSE(world.HasComponent<TagFrozen>(e));
    EXPECT_TRUE(world.HasComponent<Pos>(e));
    EXPECT_EQ(world.TryGet<Pos>(e)->X, 1.f);
}

// ─── Archetype transitions ────────────────────────────────────────────────────

TEST_F(EcsTest, EntitiesWithSameSignatureShareArchetype)
{
    EntityId e1 = world.CreateEntity();
    EntityId e2 = world.CreateEntity();
    world.AddComponent<Pos>(e1, {});
    world.AddComponent<Vel>(e1, {});
    world.AddComponent<Pos>(e2, {});
    world.AddComponent<Vel>(e2, {});

    // Both entities should have been placed into the same archetype.
    // Verify by checking that the archetype list does not have duplicate {Pos,Vel} entries.
    const auto& archetypes = world.GetArchetypes();
    ArchetypeSignature posVelSig;
    posVelSig.set(world.GetComponentId<Pos>());
    posVelSig.set(world.GetComponentId<Vel>());

    int count = 0;
    for (const auto& a : archetypes)
        if (a->Signature == posVelSig) ++count;
    EXPECT_EQ(count, 1);
}

TEST_F(EcsTest, DataPreservedAcrossArchetypeTransition)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 7.f, 8.f, 9.f });
    // Moving from {Pos} → {Pos, Vel} should preserve Pos data.
    world.AddComponent<Vel>(e, { 1.f, 2.f, 3.f });

    const Pos* p = world.TryGet<Pos>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->X, 7.f);
    EXPECT_EQ(p->Y, 8.f);
    EXPECT_EQ(p->Z, 9.f);
}

TEST_F(EcsTest, SwapAndPopUpdatesMovedEntityLocation)
{
    // Create enough entities to fill a chunk, then remove one from the middle
    // to trigger swap-and-pop. The swapped entity must remain accessible.
    EntityId entities[10];
    for (int i = 0; i < 10; ++i)
    {
        entities[i] = world.CreateEntity();
        world.AddComponent<Pos>(entities[i], { static_cast<float>(i), 0.f, 0.f });
    }

    // Destroy entity 4 — entity 9 (last) swaps into its row.
    world.DestroyEntity(entities[4]);

    // Entity 9 must still be alive and readable.
    EXPECT_TRUE(world.IsAlive(entities[9]));
    const Pos* p = world.TryGet<Pos>(entities[9]);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->X, 9.f);
}

// ─── Query accessor combinations ─────────────────────────────────────────────

TEST_F(EcsTest, QueryReadWrite_IteratesAllMatchingEntities)
{
    for (int i = 0; i < 50; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<Pos>(e, { 0.f, 0.f, 0.f });
        world.AddComponent<Vel>(e, { 1.f, 0.f, 0.f });
    }

    Query<Read<Vel>, Write<Pos>> q(world);
    world.AdvanceFrame();

    q.ForEachChunk([](auto& view) {
        auto vel = view.template Read<Vel>();
        auto pos = view.template Write<Pos>();
        for (uint32_t i = 0; i < view.Count(); ++i)
            pos[i].X += vel[i].X;
    });

    // Verify all 50 entities have Pos.X == 1.
    int checked = 0;
    for (int i = 0; i < 50; ++i)
    {
        // We can't easily index entities here without a reference list,
        // so we verify via a second query.
    }

    // Second pass query just counts rows.
    Query<Read<Pos>> verify(world);
    int total = 0;
    verify.ForEachChunk([&total](auto& view) {
        total += static_cast<int>(view.Count());
        auto pos = view.template Read<Pos>();
        for (uint32_t i = 0; i < view.Count(); ++i)
            EXPECT_EQ(pos[i].X, 1.f);
    });
    EXPECT_EQ(total, 50);
}

TEST_F(EcsTest, QueryWith_OnlyMatchesEntitiesWithTag)
{
    EntityId e1 = world.CreateEntity();
    world.AddComponent<HP>(e1, { 100.f });
    world.AddComponent<TagPlayer>(e1);

    EntityId e2 = world.CreateEntity();
    world.AddComponent<HP>(e2, { 80.f });
    // e2 has no TagPlayer

    Query<Read<HP>, With<TagPlayer>> q(world);
    int count = 0;
    q.ForEachChunk([&count](auto& view) {
        count += static_cast<int>(view.Count());
    });
    EXPECT_EQ(count, 1);
}

TEST_F(EcsTest, QueryWithout_ExcludesTaggedEntities)
{
    EntityId e1 = world.CreateEntity();
    world.AddComponent<HP>(e1, { 100.f });

    EntityId e2 = world.CreateEntity();
    world.AddComponent<HP>(e2, { 50.f });
    world.AddComponent<TagFrozen>(e2);

    Query<Read<HP>, Without<TagFrozen>> q(world);
    int count = 0;
    q.ForEachChunk([&count](auto& view) {
        count += static_cast<int>(view.Count());
    });
    EXPECT_EQ(count, 1);
}

TEST_F(EcsTest, QueryWithAndWithout_Combined)
{
    EntityId e1 = world.CreateEntity();
    world.AddComponent<HP>(e1, {});
    world.AddComponent<TagPlayer>(e1);

    EntityId e2 = world.CreateEntity();
    world.AddComponent<HP>(e2, {});
    world.AddComponent<TagPlayer>(e2);
    world.AddComponent<TagFrozen>(e2);

    EntityId e3 = world.CreateEntity();
    world.AddComponent<HP>(e3, {});
    // no tags

    // Matches: has TagPlayer AND does not have TagFrozen → only e1
    Query<Read<HP>, With<TagPlayer>, Without<TagFrozen>> q(world);
    int count = 0;
    q.ForEachChunk([&count](auto& view) {
        count += static_cast<int>(view.Count());
    });
    EXPECT_EQ(count, 1);
}

TEST_F(EcsTest, QueryMatchesMultipleArchetypes)
{
    // e1: {Pos}
    EntityId e1 = world.CreateEntity();
    world.AddComponent<Pos>(e1, { 1.f, 0.f, 0.f });

    // e2: {Pos, Vel}
    EntityId e2 = world.CreateEntity();
    world.AddComponent<Pos>(e2, { 2.f, 0.f, 0.f });
    world.AddComponent<Vel>(e2, {});

    // Query<Read<Pos>> should match both archetypes (2 entities total).
    Query<Read<Pos>> q(world);
    int count = 0;
    q.ForEachChunk([&count](auto& view) {
        count += static_cast<int>(view.Count());
    });
    EXPECT_EQ(count, 2);
}

TEST_F(EcsTest, QueryCacheUpdatesWhenNewArchetypeCreated)
{
    EntityId e1 = world.CreateEntity();
    world.AddComponent<Pos>(e1, { 1.f, 0.f, 0.f });

    Query<Read<Pos>> q(world);
    int count1 = 0;
    q.ForEachChunk([&count1](auto& view) { count1 += view.Count(); });
    EXPECT_EQ(count1, 1);

    // Adding a new component creates a new archetype — query must pick it up.
    EntityId e2 = world.CreateEntity();
    world.AddComponent<Pos>(e2, { 2.f, 0.f, 0.f });
    world.AddComponent<Vel>(e2, {});

    int count2 = 0;
    q.ForEachChunk([&count2](auto& view) { count2 += view.Count(); });
    EXPECT_EQ(count2, 2);
}

// ─── Change detection ────────────────────────────────────────────────────────

TEST_F(EcsTest, Changed_MatchesChunkWrittenThisFrame)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 0.f, 0.f, 0.f });
    world.AddComponent<Vel>(e, { 1.f, 0.f, 0.f });

    // Advance to frame 1 so the "previous frame" is frame 0.
    world.AdvanceFrame();

    // Write Pos via a Write<Pos> query at frame 1.
    {
        Query<Write<Pos>> writer(world);
        writer.ForEachChunk([](auto& view) {
            auto pos = view.template Write<Pos>();
            for (uint32_t i = 0; i < view.Count(); ++i)
                pos[i].X = 99.f;
        });
    }

    // Changed<Pos> with referenceFrame=0 should see the chunk (written at frame 1 > 0).
    Query<Read<Pos>, Changed<Pos>> reader(world);
    int seen = 0;
    reader.ForEachChunk([&seen](auto& view) {
        seen += view.Count();
    }, /* referenceFrame= */ 0);
    EXPECT_GT(seen, 0);
}

TEST_F(EcsTest, Changed_SkipsChunkNotWrittenThisFrame)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 0.f, 0.f, 0.f });

    // Never write to Pos; its column version stays at 0.
    world.AdvanceFrame();
    world.AdvanceFrame(); // now at frame 2

    // Query with referenceFrame=1 should skip the chunk (last written at frame 0).
    Query<Read<Pos>, Changed<Pos>> q(world);
    int seen = 0;
    q.ForEachChunk([&seen](auto& view) {
        seen += view.Count();
    }, /* referenceFrame= */ 1);
    EXPECT_EQ(seen, 0);
}

TEST_F(EcsTest, WriteAccessBumpsColumnVersion)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, {});

    world.AdvanceFrame(); // frame = 1

    // ForEachChunk with Write<Pos> must bump the column version to current frame.
    {
        Query<Write<Pos>> q(world);
        q.ForEachChunk([](auto& /*view*/) {});
    }

    // The touched chunk's LastWrittenFrame should now be 1.
    const auto& archetypes = world.GetArchetypes();
    const ComponentId posId = world.GetComponentId<Pos>();
    bool found = false;
    for (const auto& arch : archetypes)
    {
        for (const auto& col : arch->Columns)
        {
            if (col.Id == posId)
            {
                for (const auto& chunk : arch->Chunks)
                    if (!chunk->IsEmpty())
                        EXPECT_EQ(chunk->ColumnLastWrittenFrame(&col - arch->Columns.data()), 1u);
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(EcsTest, Changed_IsTrackedPerChunk)
{
    std::vector<EntityId> entities;
    entities.reserve(1400);
    for (int i = 0; i < 1400; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<Pos>(e, { static_cast<float>(i), 0.f, 0.f });
        entities.push_back(e);
    }

    const ComponentId posId = world.GetComponentId<Pos>();
    Archetype* posArch = nullptr;
    for (const auto& arch : world.GetArchetypes())
    {
        if (arch->Signature.test(posId) && arch->Signature.count() == 1)
            posArch = arch.get();
    }
    ASSERT_NE(posArch, nullptr);
    ASSERT_GE(posArch->Chunks.size(), 2u);

    const uint32_t firstChunkCount = posArch->Chunks[0]->RowCount;
    world.AdvanceFrame();
    const uint32_t posCol = posArch->Chunks[0]->FindColumn(posId);
    ASSERT_NE(posCol, UINT32_MAX);
    posArch->Chunks[0]->BumpColumnVersion(posCol, world.CurrentFrame());

    Query<Read<Pos>, Changed<Pos>> changed(world);
    uint32_t seen = 0;
    changed.ForEachChunk([&](auto& view) {
        seen += view.Count();
    }, 0);
    EXPECT_EQ(seen, firstChunkCount);
}

// ─── Command buffer ───────────────────────────────────────────────────────────

TEST_F(EcsTest, CommandBuffer_AddComponent_FlushApplies)
{
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Pos>(e, { 5.f, 6.f, 7.f });
    EXPECT_FALSE(world.HasComponent<Pos>(e)); // not applied yet
    cmds.Flush();
    EXPECT_TRUE(world.HasComponent<Pos>(e));
    const Pos* p = world.TryGet<Pos>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->X, 5.f);
}

TEST_F(EcsTest, CommandBuffer_RemoveComponent_FlushApplies)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });

    CommandBuffer cmds(world);
    cmds.RemoveComponent<Pos>(e);
    EXPECT_TRUE(world.HasComponent<Pos>(e)); // not applied yet
    cmds.Flush();
    EXPECT_FALSE(world.HasComponent<Pos>(e));
}

TEST_F(EcsTest, CommandBuffer_DestroyEntity_FlushApplies)
{
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.DestroyEntity(e);
    EXPECT_TRUE(world.IsAlive(e));
    cmds.Flush();
    EXPECT_FALSE(world.IsAlive(e));
}

TEST_F(EcsTest, CommandBuffer_CreateEntity_FlushApplies)
{
    EXPECT_EQ(world.EntityCount(), 0u);
    CommandBuffer cmds(world);
    cmds.CreateEntity();
    EXPECT_EQ(world.EntityCount(), 0u); // not applied yet
    cmds.Flush();
    EXPECT_EQ(world.EntityCount(), 1u);
}

TEST_F(EcsTest, CommandBuffer_FlushOrderPreserved)
{
    // Record: Add<Pos>, Add<Vel>. After flush both should be present.
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    cmds.AddComponent<Vel>(e, { 0.f, 1.f, 0.f });
    cmds.Flush();

    EXPECT_TRUE(world.HasComponent<Pos>(e));
    EXPECT_TRUE(world.HasComponent<Vel>(e));
    EXPECT_EQ(world.TryGet<Pos>(e)->X, 1.f);
    EXPECT_EQ(world.TryGet<Vel>(e)->Y, 1.f);
}

TEST_F(EcsTest, CommandBuffer_SkipsDeadEntityOnFlush)
{
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Pos>(e, {});
    world.DestroyEntity(e); // kill before flush
    cmds.Flush();           // should not crash
    EXPECT_FALSE(world.IsAlive(e));
}

TEST_F(EcsTest, CommandBuffer_FlushDuringActiveQueryAssertsInDebug)
{
    CommandBuffer cmds(world);
    world.PushQueryScope();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(cmds.Flush(), "CommandBuffer::Flush called while a query is active");
#endif
    world.PopQueryScope();
}

TEST_F(EcsTest, CommandBuffer_MultipleFlushesWork)
{
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    cmds.Flush();
    cmds.AddComponent<Vel>(e, { 0.f, 2.f, 0.f });
    cmds.Flush();

    EXPECT_EQ(world.TryGet<Pos>(e)->X, 1.f);
    EXPECT_EQ(world.TryGet<Vel>(e)->Y, 2.f);
}

// ─── Lifecycle hooks ──────────────────────────────────────────────────────────

TEST_F(EcsTest, OnAddHook_FiresOnDirectAddComponent)
{
    EntityId e = world.CreateEntity();
    EXPECT_EQ(g_OnAddCount, 0);
    world.AddComponent<Tracked>(e, { 42 });
    EXPECT_EQ(g_OnAddCount, 1);
}

TEST_F(EcsTest, OnRemoveHook_FiresOnDirectRemoveComponent)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Tracked>(e, { 1 });
    g_OnRemoveCount = 0;
    world.RemoveComponent<Tracked>(e);
    EXPECT_EQ(g_OnRemoveCount, 1);
}

TEST_F(EcsTest, OnAddHook_FiresOnCommandBufferFlush)
{
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Tracked>(e, { 7 });
    EXPECT_EQ(g_OnAddCount, 0);
    cmds.Flush();
    EXPECT_EQ(g_OnAddCount, 1);
}

TEST_F(EcsTest, OnRemoveHook_FiresOnCommandBufferFlush)
{
    EntityId e = world.CreateEntity();
    world.AddComponent<Tracked>(e, { 3 });
    g_OnRemoveCount = 0;
    CommandBuffer cmds(world);
    cmds.RemoveComponent<Tracked>(e);
    cmds.Flush();
    EXPECT_EQ(g_OnRemoveCount, 1);
}

TEST_F(EcsTest, LifecycleHook_StructuralMutationAssertsInDebug)
{
    EntityId e = world.CreateEntity();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.AddComponent<HookAddsMass>(e, { 1 }),
                 "lifecycle hook is active");
#endif
}

// ─── Resources ───────────────────────────────────────────────────────────────

struct GameConfig { int MaxEnemies = 100; };
struct AudioSettings { float Volume = 0.8f; };

TEST_F(EcsTest, AddAndGetResource)
{
    world.AddResource<GameConfig>();
    GameConfig& cfg = world.GetResource<GameConfig>();
    EXPECT_EQ(cfg.MaxEnemies, 100);
    cfg.MaxEnemies = 200;
    EXPECT_EQ(world.GetResource<GameConfig>().MaxEnemies, 200);
}

TEST_F(EcsTest, HasResourceReturnsFalseBeforeAdd)
{
    EXPECT_FALSE(world.HasResource<GameConfig>());
    world.AddResource<GameConfig>();
    EXPECT_TRUE(world.HasResource<GameConfig>());
}

TEST_F(EcsTest, TryGetResourceReturnsNullWhenMissing)
{
    EXPECT_EQ(world.TryGetResource<AudioSettings>(), nullptr);
}

TEST_F(EcsTest, MultipleResourceTypes)
{
    world.AddResource<GameConfig>(GameConfig{ 50 });
    world.AddResource<AudioSettings>(AudioSettings{ 0.5f });
    EXPECT_EQ(world.GetResource<GameConfig>().MaxEnemies, 50);
    EXPECT_EQ(world.GetResource<AudioSettings>().Volume, 0.5f);
}

// ─── Golden invariant tests (G2) ─────────────────────────────────────────────

TEST_F(EcsTest, Invariant_StructuralChangeDuringQueryFails)
{
    EntityId e = world.CreateEntity();
    world.PushQueryScope();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.AddComponent<Pos>(e), "query/lifecycle hook is active");
#endif
    world.PopQueryScope();
}

TEST_F(EcsTest, Invariant_WriteAccessBumpsColumnVersionConservatively)
{
    // Even if the callback does nothing, the Write<Pos> column version must
    // be bumped after ForEachChunk completes.
    EntityId e = world.CreateEntity();
    world.AddComponent<Pos>(e, {});

    world.AdvanceFrame(); // frame = 1

    Query<Write<Pos>> q(world);
    q.ForEachChunk([](auto& /*view*/) { /* write nothing */ });

    // Column version should be 1 (current frame), not 0.
    const ComponentId posId = world.GetComponentId<Pos>();
    for (const auto& arch : world.GetArchetypes())
    {
        for (const auto& col : arch->Columns)
        {
            if (col.Id == posId)
                for (const auto& chunk : arch->Chunks)
                    if (!chunk->IsEmpty())
                        EXPECT_EQ(chunk->ColumnLastWrittenFrame(&col - arch->Columns.data()), 1u);
        }
    }
}

TEST_F(EcsTest, Invariant_CommandBufferFlushOrderMatchesRecordOrder)
{
    // Verify Add then Remove in record order produces the correct final state.
    EntityId e = world.CreateEntity();
    CommandBuffer cmds(world);
    cmds.AddComponent<Pos>(e, { 1.f, 0.f, 0.f });
    cmds.AddComponent<Vel>(e, { 0.f, 1.f, 0.f });
    cmds.RemoveComponent<Pos>(e);
    cmds.Flush();

    EXPECT_FALSE(world.HasComponent<Pos>(e));
    EXPECT_TRUE(world.HasComponent<Vel>(e));
}

TEST_F(EcsTest, Invariant_RegistrationAfterEntityFails)
{
    world.CreateEntity();
#ifdef NDEBUG
    GTEST_SKIP() << "Assertion only fires in debug builds.";
#else
    EXPECT_DEATH(world.RegisterComponent<double>(),
                 "Component registration after entity creation");
#endif
}

TEST_F(EcsTest, Invariant_QueryScopeGuardNests)
{
    world.PushQueryScope();
    world.PushQueryScope();
    EXPECT_TRUE(world.InQueryScope());
    world.PopQueryScope();
    EXPECT_TRUE(world.InQueryScope());
    world.PopQueryScope();
    EXPECT_FALSE(world.InQueryScope());
}

// ─── Frame counter ───────────────────────────────────────────────────────────

TEST_F(EcsTest, FrameCounterAdvances)
{
    EXPECT_EQ(world.CurrentFrame(), 0u);
    world.AdvanceFrame();
    EXPECT_EQ(world.CurrentFrame(), 1u);
    world.AdvanceFrame();
    EXPECT_EQ(world.CurrentFrame(), 2u);
}

// ─── Large-scale stress ───────────────────────────────────────────────────────

TEST_F(EcsTest, Stress_1000EntitiesCreateAndQuery)
{
    for (int i = 0; i < 1000; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<Pos>(e, { static_cast<float>(i), 0.f, 0.f });
        world.AddComponent<Vel>(e, { 0.f, 0.f, 1.f });
    }

    int count = 0;
    Query<Read<Pos>, Write<Vel>> q(world);
    q.ForEachChunk([&count](auto& view) {
        count += view.Count();
    });
    EXPECT_EQ(count, 1000);
}

TEST_F(EcsTest, Stress_CommandBufferBulkAddRemove)
{
    // Create 500 entities with {Pos}, then use CommandBuffer to add Vel to all.
    std::vector<EntityId> entities;
    entities.reserve(500);
    for (int i = 0; i < 500; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<Pos>(e, {});
        entities.push_back(e);
    }

    CommandBuffer cmds(world);
    for (auto e : entities)
        cmds.AddComponent<Vel>(e, { 1.f, 0.f, 0.f });
    cmds.Flush();

    int withBoth = 0;
    Query<Read<Pos>, Read<Vel>> q(world);
    q.ForEachChunk([&withBoth](auto& view) { withBoth += view.Count(); });
    EXPECT_EQ(withBoth, 500);
}

TEST_F(EcsTest, Stress_CommandBufferBatchedNonContiguousAddRemovePreservesData)
{
    std::vector<EntityId> entities;
    entities.reserve(2000);
    for (int i = 0; i < 2000; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<Pos>(e, { static_cast<float>(i), 0.f, 0.f });
        world.AddComponent<Vel>(e, { static_cast<float>(i * 2), 0.f, 0.f });
        entities.push_back(e);
    }

    CommandBuffer add(world);
    for (int i = 1999; i >= 0; i -= 3)
        add.AddComponent<Mass>(entities[i], { static_cast<float>(i * 3) });
    add.Flush();

    for (int i = 0; i < 2000; ++i)
    {
        ASSERT_TRUE(world.HasComponent<Pos>(entities[i]));
        ASSERT_TRUE(world.HasComponent<Vel>(entities[i]));
        EXPECT_EQ(world.TryGet<Pos>(entities[i])->X, static_cast<float>(i));
        EXPECT_EQ(world.TryGet<Vel>(entities[i])->X, static_cast<float>(i * 2));
        EXPECT_EQ(world.HasComponent<Mass>(entities[i]), i % 3 == 1);
        if (i % 3 == 1)
            EXPECT_EQ(world.TryGet<Mass>(entities[i])->Value, static_cast<float>(i * 3));
    }

    CommandBuffer remove(world);
    for (int i = 1; i < 2000; i += 6)
        remove.RemoveComponent<Mass>(entities[i]);
    remove.Flush();

    for (int i = 0; i < 2000; ++i)
    {
        const bool shouldHaveMass = (i % 3 == 1) && (i % 6 != 1);
        EXPECT_EQ(world.HasComponent<Mass>(entities[i]), shouldHaveMass);
        EXPECT_EQ(world.TryGet<Pos>(entities[i])->X, static_cast<float>(i));
        EXPECT_EQ(world.TryGet<Vel>(entities[i])->X, static_cast<float>(i * 2));
    }
}
