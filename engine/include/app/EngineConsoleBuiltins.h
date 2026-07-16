#pragma once

#include <core/config/ConsoleConfig.h>
#include <core/config/RuntimeConfig.h>
#include <core/console/ConsoleTypes.h>
#include <profiling/RenderInstrumentation.h>
#include <render/ShadowResidency.h>

#include <functional>
#include <memory>

class ConsoleRegistry;
class ConsoleService;
class DebugService;
class DefaultRenderPipeline;
class FrameDriver;
class RuntimeFrameLoop;

namespace EngineConsoleBuiltins
{
    void RegisterConsoleCVars(ConsoleRegistry& registry,
                              DebugService& debug,
                              const EngineConsoleConfig& config);

    void RegisterRuntimeCVars(ConsoleRegistry& registry,
                              RuntimeFrameLoop& runtimeLoop,
                              EngineRuntimeConfig& runtimeConfig);

    void RegisterFramePacingCVars(ConsoleRegistry& registry,
                                  EngineRuntimeConfig& runtimeConfig,
                                  std::unique_ptr<FrameDriver>& frameDriver);

    void RegisterHostCommands(ConsoleService& console,
                              std::function<void()> quitHandler);

    // Commands that reach into the render pipeline; registered by the engine
    // once the pipeline exists.
    void RegisterRenderCommands(ConsoleRegistry& registry,
                                DefaultRenderPipeline& pipeline);

    // The render.shadow.* budget cvars with their registration clamps applied;
    // the game pipeline and the editor preview read the same budgets so the
    // editor's readout predicts the game's arbitration. A null registry yields
    // the registered defaults.
    [[nodiscard]] ShadowResidencyBudgets ReadShadowResidencyBudgets(
        const ConsoleRegistry* registry);

    // render.profile.mode: writes the parsed mode into `pendingMode`; the
    // engine's frame latch applies it at the top of the next extract phase.
    // Registered only when render profiling is compiled in.
    void RegisterProfilingCVars(ConsoleRegistry& registry,
                                RenderProfileMode& pendingMode);

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // render.capture.start [n] / stop / write <path>. Arming gates on the
    // PENDING mode so a launch line can set the mode and arm in one breath;
    // records only append once the latch makes Capture active. Write
    // serializes whatever is buffered (JSON, or CSV when the path ends in
    // .csv) with a cvar snapshot.
    void RegisterCaptureCommands(ConsoleRegistry& registry,
                                 RenderCapture& capture,
                                 const RenderProfileMode& pendingMode);
#endif

    ConsoleResult ApplyConfigAssignments(ConsoleService& console,
                                         const EngineConsoleConfig& config);
}
