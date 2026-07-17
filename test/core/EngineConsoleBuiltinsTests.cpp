#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <profiling/RenderCapture.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

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
    EXPECT_NE(registry.FindCVar("render.shadow.darkness"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.softness"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.bias_const"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.bias_slope"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.max_spot"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.max_point"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.max_views_per_frame"), nullptr);
    EXPECT_NE(registry.FindCVar("render.shadow.min_invalidated_views_per_frame"), nullptr);
}

TEST(EngineConsoleBuiltins, ShadowBudgetsReadCVarsAndDefaultWithoutARegistry)
{
    const ShadowResidencyBudgets defaults =
        EngineConsoleBuiltins::ReadShadowResidencyBudgets(nullptr);
    EXPECT_EQ(defaults.MaxSlots, 8u);
    EXPECT_EQ(defaults.MaxPointSlots, 4u);
    EXPECT_EQ(defaults.MaxViewsPerFrame, 12u);
    EXPECT_EQ(defaults.MinInvalidatedViewsPerFrame, 1u);

    ConsoleRegistry registry;
    RuntimeFrameLoop loop;
    EngineRuntimeConfig runtime;
    EngineConsoleBuiltins::RegisterRuntimeCVars(registry, loop, runtime);

    EXPECT_TRUE(registry.SetCVar("render.shadow.max_spot", 3.0, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_TRUE(registry.SetCVar("render.shadow.max_point", 2.0, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_TRUE(registry.SetCVar("render.shadow.max_views_per_frame", 0.0, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_TRUE(registry.SetCVar("render.shadow.min_invalidated_views_per_frame", 2.0,
                                 { "test" }, ConsolePhase::EngineReady).Succeeded());

    const ShadowResidencyBudgets budgets =
        EngineConsoleBuiltins::ReadShadowResidencyBudgets(&registry);
    EXPECT_EQ(budgets.MaxSlots, 3u);
    EXPECT_EQ(budgets.MaxPointSlots, 2u);
    EXPECT_EQ(budgets.MaxViewsPerFrame, 0u);
    EXPECT_EQ(budgets.MinInvalidatedViewsPerFrame, 2u);
}

TEST(EngineConsoleBuiltins, ProfileModeCVarWritesThePendingModeAndIgnoresUnknowns)
{
    ConsoleRegistry registry;
    RenderProfileMode pending = RenderProfileMode::Off;
    EngineConsoleBuiltins::RegisterProfilingCVars(registry, pending);

    EXPECT_TRUE(registry.SetCVar("render.profile.mode", std::string{ "gpu" },
                                 { "test" }, ConsolePhase::EngineReady).Succeeded());
    EXPECT_EQ(pending, RenderProfileMode::Gpu);

    EXPECT_TRUE(registry.SetCVar("render.profile.mode", std::string{ "bogus" },
                                 { "test" }, ConsolePhase::EngineReady).Succeeded());
    EXPECT_EQ(pending, RenderProfileMode::Gpu);

    EXPECT_TRUE(registry.SetCVar("render.profile.mode", std::string{ "off" },
                                 { "test" }, ConsolePhase::EngineReady).Succeeded());
    EXPECT_EQ(pending, RenderProfileMode::Off);
}

#ifdef SENCHA_ENABLE_RENDER_PROFILING
TEST(EngineConsoleBuiltins, CaptureCommandsGateOnModeAndWriteParseableJson)
{
    ConsoleService console;
    console.AdvancePhase(ConsolePhase::EngineReady);
    RenderCapture capture;
    RenderProfileMode pendingMode = RenderProfileMode::Off;
    EngineConsoleBuiltins::RegisterCaptureCommands(
        console.Registry(), capture, pendingMode);

    ConsoleResult denied = console.ExecuteTokens(
        { "render.capture.start" }, { "test" });
    EXPECT_NE(denied.Status, ConsoleStatus::Ok);
    EXPECT_FALSE(capture.IsRecording());

    pendingMode = RenderProfileMode::Capture;
    EXPECT_TRUE(console.ExecuteTokens(
        { "render.capture.start", "8" }, { "test" }).Succeeded());
    EXPECT_TRUE(capture.IsRecording());

    TimingFrameSample timing;
    timing.RawDtSeconds = 0.008;
    RenderStats stats;
    stats.FrameIndex = 42;
    capture.Append(timing, stats);

    EXPECT_TRUE(console.ExecuteTokens(
        { "render.capture.stop" }, { "test" }).Succeeded());
    EXPECT_FALSE(capture.IsRecording());
    EXPECT_EQ(capture.Size(), 1u);

    const std::string path = (std::filesystem::temp_directory_path()
        / "sencha_capture_test.json").string();
    EXPECT_TRUE(console.ExecuteTokens(
        { "render.capture.write", path }, { "test" }).Succeeded());

    std::ifstream file(path);
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    JsonParseError error;
    const std::optional<JsonValue> parsed = JsonParse(buffer.str(), &error);
    ASSERT_TRUE(parsed.has_value()) << error.Message;
    EXPECT_EQ(parsed->Find("frame_count")->AsNumber(), 1.0);
    std::filesystem::remove(path);
}
#endif

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
