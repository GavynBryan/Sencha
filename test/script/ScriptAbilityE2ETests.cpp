#include "ScriptTestSupport.h"

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <script/ScriptAbilitySystem.h>
#include <script/ScriptCompiler.h>
#include <script/ScriptLink.h>
#include <script/ScriptRuntime.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    // A self-contained ability that needs no physics: on start it grants a
    // tag and a countdown component and enters a state; each fixed tick
    // decrements the countdown and finishes at zero.
    constexpr std::string_view kBuff =
        "component BuffTimer {\n"
        "    remaining: i32 = 0\n"
        "}\n"
        "ability Buff {\n"
        "    param duration: i32 = 3\n"
        "    state Active\n"
        "    fn start(ctx: AbilityContext) {\n"
        "        ctx.commands.add(ctx.owner, BuffTimer { remaining: duration })\n"
        "        ctx.owner.add_tag(tag\"buff.active\")\n"
        "        enter Active\n"
        "    }\n"
        "    fn Active.fixed(ctx: AbilityContext) {\n"
        "        let owner = ctx.owner\n"
        "        owner.BuffTimer.remaining = owner.BuffTimer.remaining - 1\n"
        "        if owner.BuffTimer.remaining <= 0 {\n"
        "            owner.remove_tag(tag\"buff.active\")\n"
        "            ctx.commands.remove(owner, BuffTimer)\n"
        "            ctx.finish()\n"
        "        }\n"
        "    }\n"
        "    fn cancel(ctx: AbilityContext) {\n"
        "        ctx.owner.remove_tag(tag\"buff.active\")\n"
        "    }\n"
        "}\n";

    std::shared_ptr<const ScriptModule> Compile(std::string_view source)
    {
        ScriptCompileResult result = CompileScript("ability.t", source, {});
        EXPECT_TRUE(result.Ok) << result.Error.Message;
        return std::make_shared<const ScriptModule>(std::move(result.Module));
    }

    struct BuffTimerBytes { int32_t Remaining; };
}

TEST(ScriptAbilityE2E, StartFixedFinishLifecycle)
{
    World world;
    // Registration before any entity: tag storage, tag registry resource,
    // script runtime and ability bridge, then the module's components.
    world.RegisterComponent<GameplayTagContainer>();
    world.AddResource<GameplayTagRegistry>();
    RegisterScriptRuntime(world, 42);
    RegisterScriptAbilities(world);

    const ScriptLinkResult link = LinkScriptModule(world, Compile(kBuff));
    ASSERT_TRUE(link.Ok) << link.Error;

    const ComponentId timerId =
        world.GetComponentIdByType(MakeComponentTypeId("script.BuffTimer"));
    ASSERT_NE(timerId, InvalidComponentId);
    const std::int32_t buffDecl =
        FindScriptAbilityDeclaration(world, link.ModuleIndex, "Buff");
    ASSERT_GE(buffDecl, 0);

    const GameplayTagId activeTag = world.GetResource<GameplayTagRegistry>().FindTag("buff.active");
    ASSERT_TRUE(activeTag.IsValid());

    const EntityId owner = world.CreateEntity();
    world.AddComponent(owner, GameplayTagContainer{});

    const float aim[3] = { 0.0f, 0.0f, 1.0f };
    RequestScriptAbility(world, owner, link.ModuleIndex,
                         static_cast<std::uint32_t>(buffDecl), aim, aim);

    ScriptAbilitySystem system;

    // Tick 0: activation installs the instance and runs start (grants the
    // tag, adds BuffTimer = 3, enters Active). fixed does not run yet.
    system.Step(world, 0, 1.0f / 60.0f);
    ASSERT_TRUE(world.HasComponent<ScriptAbilityState>(owner));
    ASSERT_TRUE(world.HasComponent(owner, timerId));
    EXPECT_TRUE(world.TryGet<GameplayTagContainer>(owner)->HasExact(activeTag));
    EXPECT_EQ(static_cast<const BuffTimerBytes*>(world.GetComponentRaw(owner, timerId))->Remaining, 3);

    // Ticks 1 and 2 decrement toward zero without finishing.
    system.Step(world, 1, 1.0f / 60.0f);
    EXPECT_EQ(static_cast<const BuffTimerBytes*>(world.GetComponentRaw(owner, timerId))->Remaining, 2);
    EXPECT_TRUE(world.HasComponent<ScriptAbilityState>(owner));
    system.Step(world, 2, 1.0f / 60.0f);
    EXPECT_EQ(static_cast<const BuffTimerBytes*>(world.GetComponentRaw(owner, timerId))->Remaining, 1);

    // Tick 3: remaining hits 0, finish runs: tag revoked, timer removed, the
    // ability instance retired.
    system.Step(world, 3, 1.0f / 60.0f);
    EXPECT_FALSE(world.HasComponent<ScriptAbilityState>(owner));
    EXPECT_FALSE(world.HasComponent(owner, timerId));
    EXPECT_FALSE(world.TryGet<GameplayTagContainer>(owner)->HasExact(activeTag));
}

TEST(ScriptAbilityE2E, StartCancelRunsCancelCallback)
{
    // An ability that cancels itself in start must run its cancel callback
    // and never install a lasting instance.
    constexpr std::string_view kSelfCancel =
        "ability Blocked {\n"
        "    fn start(ctx: AbilityContext) {\n"
        "        ctx.cancel()\n"
        "        return\n"
        "    }\n"
        "    fn cancel(ctx: AbilityContext) {\n"
        "        ctx.owner.remove_tag(tag\"blocked.pending\")\n"
        "    }\n"
        "}\n";

    World world;
    world.RegisterComponent<GameplayTagContainer>();
    world.AddResource<GameplayTagRegistry>();
    RegisterScriptRuntime(world);
    RegisterScriptAbilities(world);
    const ScriptLinkResult link = LinkScriptModule(world, Compile(kSelfCancel));
    ASSERT_TRUE(link.Ok) << link.Error;
    const std::int32_t decl = FindScriptAbilityDeclaration(world, link.ModuleIndex, "Blocked");
    ASSERT_GE(decl, 0);

    const GameplayTagId pending =
        world.GetResource<GameplayTagRegistry>().FindTag("blocked.pending");

    const EntityId owner = world.CreateEntity();
    GameplayTagContainer tags{};
    tags.Grant(pending);
    world.AddComponent(owner, tags);

    RequestScriptAbility(world, owner, link.ModuleIndex,
                         static_cast<std::uint32_t>(decl), nullptr, nullptr);
    ScriptAbilitySystem system;
    system.Step(world, 0, 1.0f / 60.0f);

    // The cancel callback ran (tag revoked) and no instance survives.
    EXPECT_FALSE(world.HasComponent<ScriptAbilityState>(owner));
    EXPECT_FALSE(world.TryGet<GameplayTagContainer>(owner)->HasExact(pending));
}

TEST(ScriptAbilityE2E, AbilityContextExposesAim)
{
    // start captures the aim passed at activation into a component, proving
    // the AbilityContext prelude (aim_origin / aim_direction) reaches the VM.
    constexpr std::string_view kAimProbe =
        "component AimProbe {\n"
        "    dir: Vec3\n"
        "}\n"
        "ability Probe {\n"
        "    fn start(ctx: AbilityContext) {\n"
        "        ctx.commands.add(ctx.owner, AimProbe { dir: ctx.aim_direction })\n"
        "        ctx.finish()\n"
        "    }\n"
        "}\n";

    World world;
    RegisterScriptRuntime(world);
    RegisterScriptAbilities(world);
    const ScriptLinkResult link = LinkScriptModule(world, Compile(kAimProbe));
    ASSERT_TRUE(link.Ok) << link.Error;
    const std::int32_t decl = FindScriptAbilityDeclaration(world, link.ModuleIndex, "Probe");
    const ComponentId probeId = world.GetComponentIdByType(MakeComponentTypeId("script.AimProbe"));

    const EntityId owner = world.CreateEntity();
    const float origin[3] = { 0.0f, 0.0f, 0.0f };
    const float dir[3] = { 0.25f, -0.5f, 1.0f };
    RequestScriptAbility(world, owner, link.ModuleIndex,
                         static_cast<std::uint32_t>(decl), origin, dir);

    ScriptAbilitySystem system;
    system.Step(world, 0, 1.0f / 60.0f);

    ASSERT_TRUE(world.HasComponent(owner, probeId));
    struct Vec3Bytes { float X, Y, Z; };
    const Vec3Bytes* probe = static_cast<const Vec3Bytes*>(world.GetComponentRaw(owner, probeId));
    EXPECT_FLOAT_EQ(probe->X, 0.25f);
    EXPECT_FLOAT_EQ(probe->Y, -0.5f);
    EXPECT_FLOAT_EQ(probe->Z, 1.0f);
    // A finish on the start tick retires the instance immediately.
    EXPECT_FALSE(world.HasComponent<ScriptAbilityState>(owner));
}
