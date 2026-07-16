#pragma once

#include <core/config/ConsoleConfig.h>
#include <core/config/RuntimeConfig.h>
#include <core/console/ConsoleTypes.h>
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

    ConsoleResult ApplyConfigAssignments(ConsoleService& console,
                                         const EngineConsoleConfig& config);
}
