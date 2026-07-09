#include "ScriptTestSupport.h"

#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>

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

TEST(ScriptObserverE2E, OnAddFiresAndDefersEffectIntoNextWave)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Trigger { x: i32 = 0 }\n"
        "component Marker { n: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_add(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Marker { n: 42 })\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, /*worldSeed*/ 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId triggerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Trigger"));
    const ComponentId markerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Marker"));
    ASSERT_NE(triggerId, InvalidComponentId);
    ASSERT_NE(markerId, InvalidComponentId);

    const EntityId e = world.CreateEntity();
    // Add Trigger through a CommandBuffer flush: the flush owns the reaction wave
    // loop, so on_add(Trigger) fires synchronously and its deferred add of Marker
    // lands in the next wave.
    CommandBuffer cmd(world);
    const std::int32_t zero = 0;
    cmd.AddComponentRaw(e, triggerId, &zero, sizeof(zero), alignof(std::int32_t));
    cmd.Flush();

    ASSERT_TRUE(world.HasComponent(e, markerId));
    std::int32_t n = -1;
    std::memcpy(&n, world.GetComponentRaw(e, markerId), sizeof(n));
    EXPECT_EQ(n, 42);
}

TEST(ScriptObserverE2E, BornWithReconstructionDoesNotFireObservers)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Trigger { x: i32 = 0 }\n"
        "component Marker { n: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_add(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Marker { n: 42 })\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId triggerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Trigger"));
    const ComponentId markerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Marker"));

    const EntityId e = world.CreateEntity();
    const std::int32_t zero = 0;
    {
        World::ReconstructionScope recon(world);
        world.AddComponentRaw(e, triggerId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);
    }
    EXPECT_TRUE(world.HasComponent(e, triggerId));   // born-with
    EXPECT_FALSE(world.HasComponent(e, markerId));   // observer did not fire during load
}

TEST(ScriptObserverE2E, OnRemoveFiresBeforeDrop)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Trigger { x: i32 = 0 }\n"
        "component Marker { n: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Trigger\n"
        "    fn on_remove(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Marker { n: 7 })\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId triggerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Trigger"));
    const ComponentId markerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Marker"));

    const EntityId e = world.CreateEntity();
    const std::int32_t zero = 0;
    world.AddComponentRaw(e, triggerId, &zero, sizeof(zero), alignof(std::int32_t), nullptr);

    // Removing Trigger fires on_remove (before the row drops), which defers add
    // of Marker; the flush's wave applies it.
    CommandBuffer cmd(world);
    cmd.RemoveComponentRaw(e, triggerId, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_FALSE(world.HasComponent(e, triggerId));
    ASSERT_TRUE(world.HasComponent(e, markerId));
    std::int32_t n = -1;
    std::memcpy(&n, world.GetComponentRaw(e, markerId), sizeof(n));
    EXPECT_EQ(n, 7);
}

TEST(ScriptObserverE2E, ZeroSizeTagIsAValidTrigger)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Poisoned {}\n"
        "component Marker { n: i32 = 0 }\n"
        "observer Watch {\n"
        "    over Poisoned\n"
        "    fn on_add(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Marker { n: 5 })\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId poisonedId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Poisoned"));
    const ComponentId markerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Marker"));
    ASSERT_NE(poisonedId, InvalidComponentId);

    const EntityId e = world.CreateEntity();
    CommandBuffer cmd(world);
    cmd.AddComponentRaw(e, poisonedId, nullptr, 0, 1); // a tag: no payload
    cmd.Flush();

    ASSERT_TRUE(world.HasComponent(e, markerId));
    std::int32_t n = -1;
    std::memcpy(&n, world.GetComponentRaw(e, markerId), sizeof(n));
    EXPECT_EQ(n, 5);
}
