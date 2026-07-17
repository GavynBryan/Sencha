#include <app/EngineConsoleBuiltins.h>

#include <app/DefaultRenderPipeline.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <debug/DebugService.h>
#include <runtime/FrameDriver.h>
#include <runtime/RuntimeFrameLoop.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <profiling/RenderCapture.h>

#include <charconv>
#include <fstream>
#endif

#include <algorithm>
#include <span>
#include <string_view>
#include <variant>

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
            .Help = "Whether the engine creates its debug overlay frontend. Read once at startup.",
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
        const auto registerRenderDouble = [&registry](const char* name,
                                                       double defaultValue,
                                                       const char* help,
                                                       std::optional<double> min = {},
                                                       std::optional<double> max = {}) {
            registry.RegisterCVar({
                .Name = name,
                .Owner = "engine",
                .Type = CVarType::Double,
                .DefaultValue = defaultValue,
                .CurrentValue = defaultValue,
                .Flags = CVarFlags::Archive,
                .Help = help,
                .Source = { "renderer defaults" },
                .Min = min,
                .Max = max,
            });
        };

        registerRenderDouble("render.ambient.sky_r", 0.10,
                             "Ambient sky tint red channel in linear space.", 0.0);
        registerRenderDouble("render.ambient.sky_g", 0.12,
                             "Ambient sky tint green channel in linear space.", 0.0);
        registerRenderDouble("render.ambient.sky_b", 0.15,
                             "Ambient sky tint blue channel in linear space.", 0.0);
        registerRenderDouble("render.ambient.ground_r", 0.04,
                             "Ambient ground tint red channel in linear space.", 0.0);
        registerRenderDouble("render.ambient.ground_g", 0.03,
                             "Ambient ground tint green channel in linear space.", 0.0);
        registerRenderDouble("render.ambient.ground_b", 0.02,
                             "Ambient ground tint blue channel in linear space.", 0.0);
        registerRenderDouble("render.style.diffuse_wrap", 0.25,
                             "Diffuse wrap applied to direct lighting.", 0.0, 1.0);
        registerRenderDouble("render.style.min_ambient", 0.0,
                             "Minimum ambient channel value after hemispheric lighting.", 0.0);
        registerRenderDouble("render.exposure", 1.0,
                             "Linear exposure multiplier applied before output mapping.", 0.0);
        registerRenderDouble("render.tonemap.knee", 0.8,
                             "Output shoulder knee. Values below the knee remain unchanged.",
                             0.0, 0.999);
        registerRenderDouble("render.shadow.darkness", 1.0,
                             "Global shadow attenuation. One preserves full shadow darkness.",
                             0.0, 1.0);
        registerRenderDouble("render.shadow.softness", 1.0,
                             "Global multiplier for shadow filter width.", 0.0);
        registerRenderDouble("render.shadow.bias_const", 4.0,
                             "Constant depth bias for shadow caster rendering.", 0.0);
        registerRenderDouble("render.shadow.bias_slope", 2.0,
                             "Slope-scaled depth bias for shadow caster rendering.", 0.0);
        registerRenderDouble("render.shadow.max_spot", 8.0,
                             "Resident spot shadow slot budget.", 0.0, 8.0);
        registerRenderDouble("render.shadow.max_point", 4.0,
                             "Resident point shadow cube budget.", 0.0, 4.0);
        registerRenderDouble("render.shadow.max_views_per_frame", 12.0,
                             "Shadow depth views rendered per frame. Zero removes the clamp.",
                             0.0);
        registerRenderDouble("render.shadow.min_invalidated_views_per_frame", 1.0,
                             "Views reserved per frame for invalidated cached shadows.", 0.0);

        registry.RegisterCVar({
            .Name = "render.tonemap",
            .Owner = "engine",
            .Type = CVarType::Bool,
            .DefaultValue = true,
            .CurrentValue = true,
            .Flags = CVarFlags::Archive,
            .Help = "Enables the renderer output shoulder.",
            .Source = { "renderer defaults" },
        });

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

    void RegisterRenderCommands(ConsoleRegistry& registry,
                                DefaultRenderPipeline& pipeline)
    {
        registry.RegisterCommand({
            .Name = "render.shadow.invalidate",
            .Owner = "engine",
            .Usage = "render.shadow.invalidate",
            .Help = "Re-render every cached shadow slot.",
            .Callback = [&pipeline](ConsoleExecutionContext&,
                                    std::span<const std::string>) {
                pipeline.InvalidateShadows();
                ConsoleResult result;
                result.Info("shadow slots invalidated");
                return result;
            },
        });
    }

    void RegisterProfilingCVars(ConsoleRegistry& registry,
                                RenderProfileMode& pendingMode)
    {
        registry.RegisterCVar({
            .Name = "render.profile.mode",
            .Owner = "engine",
            .Type = CVarType::String,
            .DefaultValue = std::string{ "off" },
            .CurrentValue = std::string{ ToString(pendingMode) },
            .Flags = CVarFlags::Transient,
            .Help = "Renderer instrumentation: off, counters, gpu, or capture. "
                    "Applied at the next frame; each mode includes the ones below it.",
            .Source = { "renderer defaults" },
            .OnChange = [&pendingMode](const CVarChangeContext& ctx) {
                RenderProfileMode parsed = pendingMode;
                if (ParseRenderProfileMode(std::get<std::string>(ctx.NewValue), parsed))
                    pendingMode = parsed;
            },
        });

#ifdef SENCHA_ENABLE_RENDER_PROFILING
        registry.RegisterCVar({
            .Name = "render.debug.view",
            .Owner = "engine",
            .Type = CVarType::String,
            .DefaultValue = std::string{ "none" },
            .CurrentValue = std::string{ "none" },
            .Flags = CVarFlags::Transient | CVarFlags::Developer,
            .Help = "Development forward view: none, world_normals, normal_map, "
                    "normal_delta, diffuse, specular, emission, roughness, "
                    "light_complexity, shadow, shadow_raw, or overdraw.",
            .Source = { "renderer defaults" },
            .EnumValues = {
                "none", "world_normals", "normal_map", "normal_delta",
                "diffuse", "specular", "emission", "roughness",
                "light_complexity", "shadow", "shadow_raw", "overdraw",
            },
        });
#endif
    }

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    void RegisterCaptureCommands(ConsoleRegistry& registry,
                                 RenderCapture& capture,
                                 const RenderProfileMode& pendingMode)
    {
        registry.RegisterCommand({
            .Name = "render.capture.start",
            .Owner = "engine",
            .Usage = "render.capture.start [frames]",
            .Help = "Begin buffering per-frame render records (requires "
                    "render.profile.mode capture). Zero or no count runs until "
                    "stop or the ring capacity.",
            .Callback = [&capture, &pendingMode](ConsoleExecutionContext&,
                                                 std::span<const std::string> args) {
                ConsoleResult result;
                if (pendingMode < RenderProfileMode::Capture)
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("requires render.profile.mode capture");
                    return result;
                }
                std::size_t frames = 0;
                if (!args.empty())
                {
                    const std::string& text = args.front();
                    const auto parsed = std::from_chars(
                        text.data(), text.data() + text.size(), frames);
                    if (parsed.ec != std::errc{})
                    {
                        result.Status = ConsoleStatus::InvalidArguments;
                        result.Error("frame count must be a non-negative integer");
                        return result;
                    }
                }
                capture.Start(frames);
                result.Info(frames == 0
                    ? std::string("capture recording until stop")
                    : "capture recording " + std::to_string(frames) + " frames");
                return result;
            },
        });

        registry.RegisterCommand({
            .Name = "render.capture.stop",
            .Owner = "engine",
            .Usage = "render.capture.stop",
            .Help = "Stop buffering render records.",
            .Callback = [&capture](ConsoleExecutionContext&,
                                   std::span<const std::string>) {
                capture.Stop();
                ConsoleResult result;
                result.Info("capture stopped with "
                            + std::to_string(capture.Size()) + " frames buffered");
                return result;
            },
        });

        registry.RegisterCommand({
            .Name = "render.capture.write",
            .Owner = "engine",
            .Usage = "render.capture.write <path>",
            .Help = "Serialize the buffered records: CSV when the path ends in "
                    ".csv, else a schema-versioned JSON envelope with a cvar "
                    "snapshot.",
            .Callback = [&capture](ConsoleExecutionContext& ctx,
                                   std::span<const std::string> args) {
                ConsoleResult result;
                if (args.size() != 1)
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("expected exactly one path argument");
                    return result;
                }
                const std::string& path = args.front();
                const bool csv = path.size() >= 4
                    && path.compare(path.size() - 4, 4, ".csv") == 0;

                std::string payload;
                if (csv)
                {
                    payload = capture.SerializeCsv();
                }
                else
                {
                    std::vector<RenderCapture::MetadataPair> cvars;
                    for (const CVarMetadata* cvar : ctx.Registry.ListCVars())
                    {
                        std::string value;
                        if (const auto* b = std::get_if<bool>(&cvar->CurrentValue))
                            value = *b ? "true" : "false";
                        else if (const auto* i = std::get_if<std::int64_t>(&cvar->CurrentValue))
                            value = std::to_string(*i);
                        else if (const auto* d = std::get_if<double>(&cvar->CurrentValue))
                            value = std::to_string(*d);
                        else if (const auto* s = std::get_if<std::string>(&cvar->CurrentValue))
                            value = *s;
                        cvars.emplace_back(cvar->Name, std::move(value));
                    }
                    payload = capture.SerializeJson(cvars);
                }

                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                if (!file.is_open() || !(file << payload))
                {
                    result.Status = ConsoleStatus::InvalidArguments;
                    result.Error("failed to write '" + path + "'");
                    return result;
                }
                result.Info("wrote " + std::to_string(capture.Size()) + " frames ("
                            + std::to_string(payload.size()) + " bytes) to " + path);
                return result;
            },
        });
    }
#endif

    ShadowResidencyBudgets ReadShadowResidencyBudgets(const ConsoleRegistry* registry)
    {
        const auto readDouble = [registry](std::string_view name, float fallback)
        {
            if (registry == nullptr)
                return fallback;
            const CVarMetadata* metadata = registry->FindCVar(name);
            if (metadata == nullptr)
                return fallback;
            const double* value = std::get_if<double>(&metadata->CurrentValue);
            return value != nullptr ? static_cast<float>(*value) : fallback;
        };

        ShadowResidencyBudgets budgets;
        budgets.MaxSlots = static_cast<std::uint32_t>(std::clamp(
            readDouble("render.shadow.max_spot", static_cast<float>(kMaxSpotShadows)),
            0.0f, static_cast<float>(kMaxSpotShadows)));
        budgets.MaxPointSlots = static_cast<std::uint32_t>(std::clamp(
            readDouble("render.shadow.max_point", static_cast<float>(kMaxPointShadows)),
            0.0f, static_cast<float>(kMaxPointShadows)));
        budgets.MaxViewsPerFrame = static_cast<std::uint32_t>(std::max(
            readDouble("render.shadow.max_views_per_frame", 12.0f), 0.0f));
        budgets.MinInvalidatedViewsPerFrame = static_cast<std::uint32_t>(std::max(
            readDouble("render.shadow.min_invalidated_views_per_frame", 1.0f), 0.0f));
        return budgets;
    }

    ConsoleResult ApplyConfigAssignments(ConsoleService& console,
                                         const EngineConsoleConfig& config)
    {
        return console.ApplyAssignments(config.CVars, { .Description = "engine config" });
    }
}
