#pragma once

#include <core/config/ConsoleConfig.h>
#include <core/config/RuntimeConfig.h>
#include <core/console/ConsoleTypes.h>

#include <functional>
#include <memory>

class ConsoleRegistry;
class ConsoleService;
class DebugService;
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

    ConsoleResult ApplyConfigAssignments(ConsoleService& console,
                                         const EngineConsoleConfig& config);
}
