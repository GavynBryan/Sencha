#include "ScriptTestSupport.h"

#include <ecs/World.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptSystemTick.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

namespace
{
    std::shared_ptr<const ScriptModule> Compile(std::string_view source)
    {
        ScriptCompileResult result = CompileScript("test.t", source, {});
        EXPECT_TRUE(result.Ok) << result.Error.Message;
        return std::make_shared<const ScriptModule>(std::move(result.Module));
    }
}

TEST(ScriptSystemE2E, TicksOverMatchingEntitiesAndWritesSelf)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Counter { n: i32 = 0 }\n"
        "system Tick {\n"
        "    over Counter\n"
        "    writes Counter\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Counter.n += 1\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, /*worldSeed*/ 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId counterId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Counter"));
    ASSERT_NE(counterId, InvalidComponentId);

    // Two entities carry Counter (matched by the `over` signature); one does not.
    const EntityId a = world.CreateEntity();
    const EntityId b = world.CreateEntity();
    const EntityId other = world.CreateEntity();
    const std::int32_t zero = 0;
    world.AddComponentRaw(a, counterId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);
    world.AddComponentRaw(b, counterId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);

    ScriptSystemTick sys;
    const float dt = 1.0f / 60.0f;
    sys.Step(world, 0, dt);
    sys.Step(world, 1, dt);
    sys.Step(world, 2, dt);

    auto readN = [&](EntityId e) {
        std::int32_t n = -1;
        std::memcpy(&n, world.GetComponentRaw(e, counterId), sizeof(n));
        return n;
    };
    EXPECT_EQ(readN(a), 3); // self write applied each of the three ticks
    EXPECT_EQ(readN(b), 3);
    EXPECT_FALSE(world.HasComponent(other, counterId)); // never matched, never gained Counter
}

TEST(ScriptSystemE2E, AfterEdgeOrdersTicks)
{
    // Declared B then A, but `B after A`, so the topo sort ticks A before B.
    // n starts 0: A does n=n*10+1 -> 1, then B does n=n*10+2 -> 12. Reverse
    // order (declaration order, no reschedule) would give 2 then 21.
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Counter { n: i32 = 0 }\n"
        "system B {\n"
        "    over Counter\n"
        "    writes Counter\n"
        "    after A\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Counter.n = ctx.entity.Counter.n * 10 + 2\n"
        "    }\n"
        "}\n"
        "system A {\n"
        "    over Counter\n"
        "    writes Counter\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        ctx.entity.Counter.n = ctx.entity.Counter.n * 10 + 1\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId counterId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Counter"));
    ASSERT_NE(counterId, InvalidComponentId);

    const EntityId e = world.CreateEntity();
    const std::int32_t zero = 0;
    world.AddComponentRaw(e, counterId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);

    ScriptSystemTick sys;
    sys.Step(world, 0, 1.0f / 60.0f);

    std::int32_t n = -1;
    std::memcpy(&n, world.GetComponentRaw(e, counterId), sizeof(n));
    EXPECT_EQ(n, 12); // A ran before B
}

TEST(ScriptSystemE2E, WithoutExcludesEntities)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Counter { n: i32 = 0 }\n"
        "component Frozen { x: i32 = 0 }\n"
        "system Tick {\n"
        "    over Counter\n"
        "    without Frozen\n"
        "    writes Counter\n"
        "    fn fixed(ctx: FixedContext) { ctx.entity.Counter.n += 1 }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId counterId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Counter"));
    const ComponentId frozenId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Frozen"));
    ASSERT_NE(counterId, InvalidComponentId);
    ASSERT_NE(frozenId, InvalidComponentId);

    const EntityId active = world.CreateEntity();
    const EntityId frozen = world.CreateEntity();
    const std::int32_t zero = 0;
    world.AddComponentRaw(active, counterId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);
    world.AddComponentRaw(frozen, counterId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);
    world.AddComponentRaw(frozen, frozenId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);

    ScriptSystemTick sys;
    sys.Step(world, 0, 1.0f / 60.0f);
    sys.Step(world, 1, 1.0f / 60.0f);

    auto readN = [&](EntityId e) {
        std::int32_t n = -1;
        std::memcpy(&n, world.GetComponentRaw(e, counterId), sizeof(n));
        return n;
    };
    EXPECT_EQ(readN(active), 2); // ticked (no Frozen)
    EXPECT_EQ(readN(frozen), 0); // excluded by `without Frozen`
}
