#include <app/EngineConsoleBuiltins.h>

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <debug/DebugService.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>

namespace EngineConsoleBuiltins
{
    void RegisterConsoleCVars(ConsoleRegistry& registry,
                              DebugService& debug,
                              const EngineConsoleConfig& config)
    {
        registry.RegisterCVar({
            .Name = "console.ui_enabled",
            .Owner = "engine",
            .Type = CVarType::Bool,
            .DefaultValue = config.UiEnabled,
            .CurrentValue = config.UiEnabled,
            .Flags = CVarFlags::Archive,
            .Help = "Enables the graphical console frontend when a host provides one.",
            .Source = { "engine config" },
        });

        registry.RegisterCVar({
            .Name = "console.open",
            .Owner = "engine",
            .Type = CVarType::Bool,
            .DefaultValue = false,
            .CurrentValue = debug.IsOpen(),
            .Flags = CVarFlags::Transient,
            .Help = "Whether the debug console UI is open.",
            .Source = { "debug service" },
            .OnChange = [&debug](const CVarChangeContext& ctx) {
                if (std::get<bool>(ctx.NewValue))
                    debug.Open();
                else
                    debug.Close();
            },
        });

        debug.SetOpenChangedCallback([&registry](bool open) {
            (void)registry.SetCVar(
                "console.open",
                open,
                { .Description = "debug service" },
                ConsolePhase::EngineReady,
                true);
        });

        registry.RegisterCVar({
            .Name = "console.history_capacity",
            .Owner = "engine",
            .Type = CVarType::Int,
            .DefaultValue = static_cast<std::int64_t>(config.HistoryCapacity),
            .CurrentValue = static_cast<std::int64_t>(config.HistoryCapacity),
            .Flags = CVarFlags::Archive,
            .Help = "Maximum number of command history entries frontends should keep.",
            .Source = { "engine config" },
            .Min = 1.0,
        });
    }

    void RegisterRuntimeCVars(ConsoleRegistry& registry,
                              RuntimeFrameLoop& runtimeLoop,
                              EngineRuntimeConfig& runtimeConfig)
    {
        registry.RegisterCVar({
            .Name = "time.timescale",
            .Owner = "engine",
            .Type = CVarType::Double,
            .DefaultValue = 1.0,
            .CurrentValue = static_cast<double>(runtimeLoop.GetSimulationTimescale()),
            .Flags = CVarFlags::Transient,
            .Help = "Simulation timescale. 0 pauses fixed-tick simulation.",
            .Source = { "runtime loop" },
            .Min = 0.0,
            .OnChange = [&runtimeLoop](const CVarChangeContext& ctx) {
                runtimeLoop.SetSimulationTimescale(
                    static_cast<float>(std::get<double>(ctx.NewValue)));
            },
        });

        registry.RegisterCVar({
            .Name = "time.fixed_tick_rate",
            .Owner = "engine",
            .Type = CVarType::Double,
            .DefaultValue = runtimeConfig.FixedTickRate,
            .CurrentValue = runtimeConfig.FixedTickRate,
            .Flags = CVarFlags::Archive | CVarFlags::InitOnly,
            .Help = "Fixed simulation tick rate. Init-only once systems are registered.",
            .Source = { "engine config" },
            .Min = 0.001,
            .OnChange = [&runtimeLoop, &runtimeConfig](const CVarChangeContext& ctx) {
                runtimeConfig.FixedTickRate = std::get<double>(ctx.NewValue);
                runtimeLoop.GetSimulationClock().SetFixedTickRate(runtimeConfig.FixedTickRate);
            },
        });
    }

    void RegisterFramePacingCVars(ConsoleRegistry& registry,
                                  EngineRuntimeConfig& runtimeConfig,
                                  std::unique_ptr<FrameDriver>& frameDriver)
    {
        registry.RegisterCVar({
            .Name = "r.target_fps",
            .Owner = "engine",
            .Type = CVarType::Double,
            .DefaultValue = runtimeConfig.TargetFps,
            .CurrentValue = runtimeConfig.TargetFps,
            .Flags = CVarFlags::Archive,
            .Help = "Frame pacing target in frames per second. 0 disables pacing.",
            .Source = { "engine config" },
            .Min = 0.0,
            .OnChange = [&runtimeConfig, &frameDriver](const CVarChangeContext& ctx) {
                runtimeConfig.TargetFps = std::get<double>(ctx.NewValue);
                if (frameDriver)
                    frameDriver->SetTargetFps(runtimeConfig.TargetFps);
            },
        });
    }

    void RegisterHostCommands(ConsoleService& console,
                              std::function<void()> quitHandler)
    {
        console.SetQuitHandler(std::move(quitHandler));
    }

    ConsoleResult ApplyConfigAssignments(ConsoleService& console,
                                         const EngineConsoleConfig& config)
    {
        return console.ApplyAssignments(config.CVars, { .Description = "engine config" });
    }
}
