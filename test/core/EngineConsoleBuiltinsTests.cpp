#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>

#include <memory>

namespace
{
    struct DebugFixture
    {
        LoggingProvider Logging;
        DebugLogSink Sink;
        DebugService Debug{ Logging, Sink };
    };
}

TEST(EngineConsoleBuiltins, RegistersExpectedEngineCVars)
{
    ConsoleService console;
    DebugFixture debug;
    EngineRuntimeConfig runtime;
    RuntimeFrameLoop loop;
    std::unique_ptr<FrameDriver> driver;

    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug.Debug, {});
    EngineConsoleBuiltins::RegisterRuntimeCVars(registry, loop, runtime);
    EngineConsoleBuiltins::RegisterFramePacingCVars(registry, runtime, driver);

    EXPECT_NE(registry.FindCVar("console.ui_enabled"), nullptr);
    EXPECT_NE(registry.FindCVar("console.open"), nullptr);
    EXPECT_NE(registry.FindCVar("console.history_capacity"), nullptr);
    EXPECT_NE(registry.FindCVar("r.target_fps"), nullptr);
    EXPECT_NE(registry.FindCVar("time.timescale"), nullptr);
    EXPECT_NE(registry.FindCVar("time.fixed_tick_rate"), nullptr);
}

TEST(EngineConsoleBuiltins, ConfigAssignmentsSetRegisteredAndQueueUnknownCVars)
{
    ConsoleService console;
    DebugFixture debug;
    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug.Debug, {});

    EngineConsoleConfig config;
    config.CVars.push_back({ "console.history_capacity", "32" });
    config.CVars.push_back({ "game.future.value", "hello" });

    ConsoleResult result = EngineConsoleBuiltins::ApplyConfigAssignments(console, config);
    (void)result;

    const CVarMetadata* history = registry.FindCVar("console.history_capacity");
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(std::get<std::int64_t>(history->CurrentValue), 32);
    ASSERT_EQ(registry.PendingAssignments().size(), 1u);
    EXPECT_EQ(registry.PendingAssignments()[0].Name, "game.future.value");
}

TEST(EngineConsoleBuiltins, ConsoleOpenReflectsDebugServiceWithoutDesync)
{
    ConsoleService console;
    DebugFixture debug;
    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug.Debug, {});

    EXPECT_FALSE(debug.Debug.IsOpen());
    EXPECT_TRUE(registry.SetCVar("console.open", true, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_TRUE(debug.Debug.IsOpen());
    ASSERT_NE(registry.FindCVar("console.open"), nullptr);
    EXPECT_TRUE(std::get<bool>(registry.FindCVar("console.open")->CurrentValue));

    debug.Debug.Close();
    EXPECT_FALSE(debug.Debug.IsOpen());
    EXPECT_FALSE(std::get<bool>(registry.FindCVar("console.open")->CurrentValue));
}

TEST(EngineConsoleBuiltins, RuntimeCVarsUpdateRuntimeLoopAndRespectInitOnly)
{
    ConsoleRegistry registry;
    RuntimeFrameLoop loop;
    EngineRuntimeConfig runtime;

    EngineConsoleBuiltins::RegisterRuntimeCVars(registry, loop, runtime);

    EXPECT_TRUE(registry.SetCVar("time.timescale", 0.25, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_FLOAT_EQ(loop.GetSimulationTimescale(), 0.25f);

    EXPECT_TRUE(registry.SetCVar("time.fixed_tick_rate", 120.0, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_DOUBLE_EQ(runtime.FixedTickRate, 120.0);
    EXPECT_DOUBLE_EQ(loop.GetSimulationClock().GetFixedDt(), 1.0 / 120.0);

    ConsoleResult late = registry.SetCVar("time.fixed_tick_rate", 30.0, { "test" },
                                          ConsolePhase::SystemsRegistered);
    EXPECT_EQ(late.Status, ConsoleStatus::PhaseNotReady);
    EXPECT_DOUBLE_EQ(runtime.FixedTickRate, 120.0);
}

TEST(EngineConsoleBuiltins, FramePacingCVarUpdatesRuntimeConfigAndDriver)
{
    ConsoleRegistry registry;
    RuntimeFrameLoop loop;
    EngineRuntimeConfig runtime;
    std::unique_ptr<FrameDriver> driver = std::make_unique<FrameDriver>(loop);

    EngineConsoleBuiltins::RegisterFramePacingCVars(registry, runtime, driver);

    EXPECT_TRUE(registry.SetCVar("r.target_fps", 144.0, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_DOUBLE_EQ(runtime.TargetFps, 144.0);
    EXPECT_DOUBLE_EQ(driver->GetTargetFps(), 144.0);
}

TEST(EngineConsoleBuiltins, OwnerUnregisterDoesNotRemoveEngineBuiltins)
{
    ConsoleService console;
    DebugFixture debug;
    ConsoleRegistry& registry = console.Registry();
    EngineConsoleBuiltins::RegisterConsoleCVars(registry, debug.Debug, {});
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "game.foo",
        .Owner = "game",
        .Type = CVarType::String,
        .DefaultValue = std::string{},
        .CurrentValue = std::string{},
    }));

    registry.UnregisterOwner("game");
    EXPECT_EQ(registry.FindCVar("game.foo"), nullptr);
    EXPECT_NE(registry.FindCVar("console.open"), nullptr);
}
