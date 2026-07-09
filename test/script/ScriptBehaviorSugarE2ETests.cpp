#include "ScriptTestSupport.h"

#include <ecs/CommandBuffer.h>
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

// A behavior with config/runtime desugars to a #[config] Config + #[runtime]
// State component and BActivate/BFixed systems. With per-system flush, an entity
// carrying only Config gets State added by BActivate and then ticked by BFixed
// in the same tick.
TEST(ScriptBehaviorSugarE2E, ActivationAddsStateThenFixedRunsSameTick)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime { n: i32 = 0 }\n"
        "    fn fixed(ctx: FixedContext) {\n"
        "        runtime.n += config.step\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, /*worldSeed*/ 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId configId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterConfig"));
    const ComponentId stateId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterState"));
    ASSERT_NE(configId, InvalidComponentId);
    ASSERT_NE(stateId, InvalidComponentId);

    // The authored attach point: an entity with Config, no State.
    const EntityId e = world.CreateEntity();
    struct ConfigBytes { std::int32_t step; };
    const ConfigBytes cfg{ 2 };
    world.AddComponentRaw(e, configId, &cfg, sizeof(cfg), alignof(std::int32_t), nullptr);

    auto readN = [&]() {
        std::int32_t n = -1;
        std::memcpy(&n, world.GetComponentRaw(e, stateId), sizeof(n));
        return n;
    };

    ScriptSystemTick sys;
    const float dt = 1.0f / 60.0f;

    sys.Step(world, 0, dt); // BActivate adds State{0} (flushed), then BFixed: n += 2
    ASSERT_TRUE(world.HasComponent(e, stateId));
    EXPECT_EQ(readN(), 2);

    sys.Step(world, 1, dt); // BActivate no longer matches (has State); BFixed: n += 2
    EXPECT_EQ(readN(), 4);
}

// spawn straight-line initializers fill the State record BActivate adds, reading
// config live. The State is created fully initialized (not defaults), so BFixed
// runs against the spawn-set values the same tick.
TEST(ScriptBehaviorSugarE2E, SpawnInitializesStateFromConfig)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "behavior Counter {\n"
        "    config { step: i32 = 0 }\n"
        "    runtime {\n"
        "        n: i32 = 0\n"
        "        base: i32 = 0\n"
        "    }\n"
        "    fn spawn(ctx: FixedContext) {\n"
        "        runtime.n = config.step\n"
        "        runtime.base = 100\n"
        "    }\n"
        "    fn fixed(ctx: FixedContext) { runtime.n += config.step }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId configId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterConfig"));
    const ComponentId stateId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterState"));
    ASSERT_NE(configId, InvalidComponentId);
    ASSERT_NE(stateId, InvalidComponentId);

    const EntityId e = world.CreateEntity();
    struct ConfigBytes { std::int32_t step; };
    const ConfigBytes cfg{ 5 };
    world.AddComponentRaw(e, configId, &cfg, sizeof(cfg), alignof(std::int32_t), nullptr);

    struct StateBytes { std::int32_t n; std::int32_t base; };
    auto readState = [&]() {
        StateBytes s{ -1, -1 };
        std::memcpy(&s, world.GetComponentRaw(e, stateId), sizeof(s));
        return s;
    };

    ScriptSystemTick sys;
    sys.Step(world, 0, 1.0f / 60.0f); // activate: State{n:5, base:100}; fixed: n += 5
    ASSERT_TRUE(world.HasComponent(e, stateId));
    const StateBytes s = readState();
    EXPECT_EQ(s.n, 10);    // spawn 5 (config.step) + fixed 5
    EXPECT_EQ(s.base, 100); // spawn constant, untouched by fixed
}

// Removing Config (the attach point) fires BDetach, which removes State, which
// fires BDespawn. The whole chain runs through the reaction waves of one flush.
TEST(ScriptBehaviorSugarE2E, RemovingConfigDetachesStateAndFiresDespawn)
{
    std::shared_ptr<const ScriptModule> module = Compile(
        "component Gravestone {}\n"
        "behavior Counter {\n"
        "    config { step: i32 = 2 }\n"
        "    runtime { n: i32 = 0 }\n"
        "    fn fixed(ctx: FixedContext) { runtime.n += config.step }\n"
        "    fn despawn(ctx: ObserveCtx) {\n"
        "        ctx.commands.add(ctx.entity, Gravestone {})\n"
        "    }\n"
        "}\n");

    World world;
    RegisterScriptRuntime(world, 0);
    const ScriptLinkResult link = LinkScriptModule(world, module);
    ASSERT_TRUE(link.Ok) << link.Error;
    const ComponentId configId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterConfig"));
    const ComponentId stateId =
        world.GetComponentIdByType(MakeComponentTypeId("script.CounterState"));
    const ComponentId graveId =
        world.GetComponentIdByType(MakeComponentTypeId("script.Gravestone"));
    ASSERT_NE(configId, InvalidComponentId);
    ASSERT_NE(stateId, InvalidComponentId);
    ASSERT_NE(graveId, InvalidComponentId);

    const EntityId e = world.CreateEntity();
    struct ConfigBytes { std::int32_t step; };
    const ConfigBytes cfg{ 2 };
    world.AddComponentRaw(e, configId, &cfg, sizeof(cfg), alignof(std::int32_t), nullptr);

    ScriptSystemTick sys;
    sys.Step(world, 0, 1.0f / 60.0f); // activate -> State present
    ASSERT_TRUE(world.HasComponent(e, stateId));

    // Remove Config through a flush: on_remove(Config) -> BDetach defers remove
    // State -> on_remove(State) -> BDespawn defers add Gravestone.
    CommandBuffer cmd(world);
    cmd.RemoveComponentRaw(e, configId, sizeof(std::int32_t));
    cmd.Flush();

    EXPECT_FALSE(world.HasComponent(e, configId));
    EXPECT_FALSE(world.HasComponent(e, stateId));       // detached
    EXPECT_TRUE(world.HasComponent(e, graveId));        // despawn ran
}
