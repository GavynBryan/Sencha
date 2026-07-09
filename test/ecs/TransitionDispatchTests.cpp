#include <ecs/TransitionDispatch.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// A component carrying a native ComponentTraits hook, so the typed AddComponent
// path can be checked to still fire the native hook (routing preserved) and to
// fire the runtime dispatch after it. Declared at global scope: the type-id key
// and the ComponentTraits specialization must live there.
struct DispatchMarked
{
    int v = 0;
};
SENCHA_DECLARE_COMPONENT_TYPE(DispatchMarked, "test.dispatch_marked");

namespace
{
    std::vector<std::string>* g_log = nullptr;
}

template <>
struct ComponentTraits<DispatchMarked>
{
    static void OnAdd(DispatchMarked& m, World&, EntityId)
    {
        if (g_log) g_log->push_back("native_add:" + std::to_string(m.v));
    }
    static void OnRemove(const DispatchMarked& m, World&, EntityId)
    {
        if (g_log) g_log->push_back("native_remove:" + std::to_string(m.v));
    }
};

namespace
{
    constexpr ComponentTypeId kHealth = MakeComponentTypeId("test.dispatch_health");
    constexpr ComponentTypeId kTag    = MakeComponentTypeId("test.dispatch_tag");
    constexpr ComponentTypeId kPlain  = MakeComponentTypeId("test.dispatch_plain");

    struct Rec
    {
        std::vector<std::string> events;
    };

    // Records presence (the component is live at the fire point) and the value.
    void AddCb(World& w, EntityId e, ComponentId id, const void* comp, void* ctx)
    {
        auto* r = static_cast<Rec*>(ctx);
        int v = -1;
        if (comp) std::memcpy(&v, comp, sizeof(int));
        r->events.push_back("add:" + std::to_string(w.HasComponent(e, id) ? 1 : 0)
                            + ":" + std::to_string(v));
    }
    void RemoveCb(World& w, EntityId e, ComponentId id, const void* comp, void* ctx)
    {
        auto* r = static_cast<Rec*>(ctx);
        int v = -1;
        if (comp) std::memcpy(&v, comp, sizeof(int));
        r->events.push_back("remove:" + std::to_string(w.HasComponent(e, id) ? 1 : 0)
                            + ":" + std::to_string(v));
    }
    void MarkedAddCb(World&, EntityId, ComponentId, const void* comp, void*)
    {
        const auto* m = static_cast<const DispatchMarked*>(comp);
        if (g_log) g_log->push_back("dispatch_add:" + std::to_string(m ? m->v : -1));
    }
    void MarkedRemoveCb(World&, EntityId, ComponentId, const void* comp, void*)
    {
        const auto* m = static_cast<const DispatchMarked*>(comp);
        if (g_log) g_log->push_back("dispatch_remove:" + std::to_string(m ? m->v : -1));
    }
}

TEST(TransitionDispatch, FiresOnRawAddAndRemoveWithLivePresentComponent)
{
    World world;
    Rec rec;
    const ComponentId id =
        world.RegisterComponentRaw("Health", kHealth, sizeof(int), alignof(int), false);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ &AddCb, &RemoveCb, nullptr, &rec });

    const EntityId e = world.CreateEntity();
    const int hp = 100;
    world.AddComponentRaw(e, id, &hp, sizeof(hp), alignof(int), nullptr);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "add:1:100");

    world.RemoveComponentRaw(e, id, nullptr);
    ASSERT_EQ(rec.events.size(), 2u);
    EXPECT_EQ(rec.events[1], "remove:1:100"); // present at fire, before the drop
    EXPECT_FALSE(world.HasComponent(e, id));
}

TEST(TransitionDispatch, DestroyFiresRemoveBeforeDrop)
{
    World world;
    Rec rec;
    const ComponentId id =
        world.RegisterComponentRaw("Health", kHealth, sizeof(int), alignof(int), false);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ &AddCb, &RemoveCb, nullptr, &rec });

    const EntityId e = world.CreateEntity();
    const int hp = 55;
    world.AddComponentRaw(e, id, &hp, sizeof(hp), alignof(int), nullptr);
    rec.events.clear();

    world.DestroyEntity(e);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "remove:1:55");
    EXPECT_FALSE(world.IsAlive(e));
}

TEST(TransitionDispatch, TagTriggerFiresDecoupledFromSize)
{
    World world;
    Rec rec;
    const ComponentId tag = world.RegisterComponentRaw("Tag", kTag, 0, 1, true);
    world.RegisterTransitionDispatch(tag, TransitionDispatch{ &AddCb, &RemoveCb, nullptr, &rec });

    const EntityId e = world.CreateEntity();
    world.AddComponentRaw(e, tag, nullptr, 0, 1, nullptr);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "add:1:-1"); // present, no data column for a tag

    world.DestroyEntity(e);
    ASSERT_EQ(rec.events.size(), 2u);
    EXPECT_EQ(rec.events[1], "remove:1:-1");
}

TEST(TransitionDispatch, TypedAddRoutesThroughRawNativeHookThenDispatch)
{
    World world;
    std::vector<std::string> log;
    g_log = &log;

    const ComponentId id = world.RegisterComponent<DispatchMarked>();
    world.RegisterTransitionDispatch(id, TransitionDispatch{ &MarkedAddCb, &MarkedRemoveCb, nullptr, nullptr });

    const EntityId e = world.CreateEntity();
    world.AddComponent<DispatchMarked>(e, DispatchMarked{ 7 });
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "native_add:7");     // native ComponentTraits hook first
    EXPECT_EQ(log[1], "dispatch_add:7");   // then the runtime dispatch

    world.RemoveComponent<DispatchMarked>(e);
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[2], "dispatch_remove:7"); // dispatch fires before native remove
    EXPECT_EQ(log[3], "native_remove:7");

    g_log = nullptr;
}

TEST(TransitionDispatch, InertWhenNothingRegistered)
{
    World world;
    const ComponentId id =
        world.RegisterComponentRaw("Plain", kPlain, sizeof(int), alignof(int), false);

    const EntityId e = world.CreateEntity();
    int v = 5;
    world.AddComponentRaw(e, id, &v, sizeof(v), alignof(int), nullptr);
    EXPECT_TRUE(world.HasComponent(e, id));
    world.RemoveComponentRaw(e, id, nullptr);
    EXPECT_FALSE(world.HasComponent(e, id));

    const EntityId e2 = world.CreateEntity();
    world.AddComponentRaw(e2, id, &v, sizeof(v), alignof(int), nullptr);
    world.DestroyEntity(e2); // no dispatch registered: wholesale drop, no crash
    EXPECT_FALSE(world.IsAlive(e2));
}

TEST(TransitionDispatch, BornWithReconstructionSuppressesDispatch)
{
    World world;
    Rec rec;
    const ComponentId id =
        world.RegisterComponentRaw("Health", kHealth, sizeof(int), alignof(int), false);
    world.RegisterTransitionDispatch(id, TransitionDispatch{ &AddCb, &RemoveCb, nullptr, &rec });

    const EntityId e = world.CreateEntity();
    const int hp = 7;
    {
        World::ReconstructionScope recon(world);
        world.AddComponentRaw(e, id, &hp, sizeof(hp), alignof(int), nullptr);
    }
    EXPECT_TRUE(world.HasComponent(e, id)); // component is born-with
    EXPECT_TRUE(rec.events.empty());        // no on_add fired during reconstruction

    // After the scope, a genuinely runtime add fires normally.
    const EntityId e2 = world.CreateEntity();
    world.AddComponentRaw(e2, id, &hp, sizeof(hp), alignof(int), nullptr);
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "add:1:7");
}
