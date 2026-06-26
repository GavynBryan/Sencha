#include <gtest/gtest.h>

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleStartupScript.h>

TEST(ConsoleRegistry, CanonicalDottedNamesAndHierarchy)
{
    ConsoleRegistry registry;
    ConsoleResult result;

    EXPECT_TRUE(registry.RegisterCVar({
        .Name = "R.Target_Fps",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 60.0,
        .CurrentValue = 60.0,
    }, &result));

    EXPECT_NE(registry.FindCVar("r.target_fps"), nullptr);
    ASSERT_EQ(registry.TreeNodes().size(), 1u);
    EXPECT_EQ(registry.TreeNodes()[0], "r");
    EXPECT_FALSE(registry.RegisterCommand({
        .Name = "r.target_fps",
        .Callback = [](ConsoleExecutionContext&, std::span<const std::string>) {
            return ConsoleResult{};
        },
    }));
}

TEST(ConsoleRegistry, ValidatorsCallbacksLatchedAndResetTree)
{
    ConsoleRegistry registry;
    int callbacks = 0;
    EXPECT_TRUE(registry.RegisterCVar({
        .Name = "game.player.speed",
        .Owner = "game",
        .Type = CVarType::Double,
        .DefaultValue = 5.0,
        .CurrentValue = 5.0,
        .Flags = CVarFlags::Latched,
        .Min = 0.0,
        .OnChange = [&callbacks](const CVarChangeContext&) { ++callbacks; },
    }));

    ConsoleResult bad = registry.SetCVarFromString(
        "game.player.speed", "-1", { .Description = "test" }, ConsolePhase::EngineReady);
    EXPECT_EQ(bad.Status, ConsoleStatus::ValidationFailed);
    EXPECT_EQ(callbacks, 0);

    ConsoleResult latched = registry.SetCVarFromString(
        "game.player.speed", "8", { .Description = "test" }, ConsolePhase::SystemsRegistered);
    EXPECT_EQ(latched.Status, ConsoleStatus::Deferred);
    const CVarMetadata* cvar = registry.FindCVar("game.player.speed");
    ASSERT_NE(cvar, nullptr);
    ASSERT_TRUE(cvar->LatchedValue.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*cvar->LatchedValue), 8.0);
    EXPECT_DOUBLE_EQ(std::get<double>(cvar->CurrentValue), 5.0);

    ConsoleResult reset = registry.ResetTree("game", ConsolePhase::EngineReady);
    EXPECT_TRUE(reset.Succeeded());
    cvar = registry.FindCVar("game.player.speed");
    ASSERT_NE(cvar, nullptr);
    EXPECT_FALSE(cvar->LatchedValue.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(cvar->CurrentValue), 5.0);
}

TEST(ConsoleRegistry, PendingAssignmentsResolveOnRegistration)
{
    ConsoleRegistry registry;
    registry.QueuePendingAssignment("game.foo", "42", { .Description = "config" });

    EXPECT_TRUE(registry.RegisterCVar({
        .Name = "game.foo",
        .Owner = "game",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(0),
        .CurrentValue = static_cast<std::int64_t>(0),
    }));

    const CVarMetadata* cvar = registry.FindCVar("game.foo");
    ASSERT_NE(cvar, nullptr);
    EXPECT_EQ(std::get<std::int64_t>(cvar->CurrentValue), 42);
    EXPECT_TRUE(registry.PendingAssignments()[0].Resolved);
}

TEST(ConsoleRegistry, OverrideScopesRestoreInStackOrder)
{
    ConsoleRegistry registry;
    EXPECT_TRUE(registry.RegisterCVar({
        .Name = "test.value",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(1),
        .CurrentValue = static_cast<std::int64_t>(1),
    }));

    {
        ConsoleOverrideScope outer = registry.CreateOverrideScope(ConsolePhase::EngineReady);
        EXPECT_TRUE(outer.Set("test.value", static_cast<std::int64_t>(2)).Succeeded());
        {
            ConsoleOverrideScope inner = registry.CreateOverrideScope(ConsolePhase::EngineReady);
            EXPECT_TRUE(inner.Set("test.value", static_cast<std::int64_t>(3)).Succeeded());
            EXPECT_EQ(std::get<std::int64_t>(registry.FindCVar("test.value")->CurrentValue), 3);
        }
        EXPECT_EQ(std::get<std::int64_t>(registry.FindCVar("test.value")->CurrentValue), 2);
    }
    EXPECT_EQ(std::get<std::int64_t>(registry.FindCVar("test.value")->CurrentValue), 1);
}

TEST(ConsoleService, PhaseDefersStartupCommands)
{
    ConsoleService console;
    int calls = 0;
    console.Registry().RegisterCommand({
        .Name = "game.start",
        .Usage = "game.start",
        .RequiredPhase = ConsolePhase::GameplayStarted,
        .Callback = [&calls](ConsoleExecutionContext&, std::span<const std::string>) {
            ++calls;
            return ConsoleResult{};
        },
    });

    ConsoleStartupScript script;
    script.Add({ .Args = { "game.start" }, .Source = { .Description = "test" } });
    ConsoleResult result = console.ExecuteStartupScript(script);
    EXPECT_EQ(result.Status, ConsoleStatus::Ok);
    EXPECT_EQ(calls, 0);
    ASSERT_EQ(console.DeferredCommands().size(), 1u);

    ConsoleResult interactive = console.ExecuteLine("game.start");
    EXPECT_EQ(interactive.Status, ConsoleStatus::PhaseNotReady);

    console.AdvancePhase(ConsolePhase::GameplayStarted);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(console.DeferredCommands().empty());
}

TEST(ConsoleStartupScript, ParsesArgvAndQuotedScriptLines)
{
    const char* argv[] = {
        "app",
        "--game",
        "leveldemo",
        "+map",
        "levels/test level.json",
        "+set",
        "r.target_fps",
        "144",
    };
    ConsoleStartupScript cli =
        ConsoleStartupScript::FromArgv(8, const_cast<char**>(argv));
    ASSERT_EQ(cli.Commands().size(), 2u);
    EXPECT_EQ(cli.Commands()[0].Args[0], "map");
    EXPECT_EQ(cli.Commands()[0].Args[1], "levels/test level.json");
    EXPECT_EQ(cli.Commands()[1].Args[0], "set");

    std::vector<ConsoleParseDiagnostic> diagnostics;
    ConsoleStartupScript file = ConsoleStartupScript::ParseText(
        "echo \"hello world\" // comment\nset a.b \"quoted value\"\n",
        "test.cfg",
        &diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(file.Commands().size(), 2u);
    EXPECT_EQ(file.Commands()[0].Args[1], "hello world");
    EXPECT_EQ(file.Commands()[1].Args[2], "quoted value");
}
