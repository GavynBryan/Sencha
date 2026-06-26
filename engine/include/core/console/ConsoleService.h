#pragma once

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleStartupScript.h>
#include <core/config/ConsoleConfig.h>

#include <functional>
#include <string>
#include <vector>

class ConsoleService
{
public:
    ConsoleService();

    [[nodiscard]] ConsoleRegistry& Registry() { return RegistryInstance; }
    [[nodiscard]] const ConsoleRegistry& Registry() const { return RegistryInstance; }

    [[nodiscard]] ConsolePhase Phase() const { return CurrentPhase; }
    void AdvancePhase(ConsolePhase phase);

    ConsoleResult Execute(const ConsoleCommandLine& line, bool deferrable = false);
    ConsoleResult ExecuteTokens(std::vector<std::string> args,
                                ConsoleValueSource source = {},
                                bool deferrable = false);
    ConsoleResult ExecuteLine(std::string_view text,
                              ConsoleValueSource source = {},
                              bool deferrable = false);
    ConsoleResult ExecuteStartupScript(const ConsoleStartupScript& script);
    ConsoleResult ApplyAssignments(const std::vector<EngineConsoleCVarAssignment>& assignments,
                                   ConsoleValueSource source);

    void SetQuitHandler(std::function<void()> handler) { QuitHandler = std::move(handler); }
    void SetMapHandler(std::function<ConsoleResult(std::string_view)> handler)
    {
        MapHandler = std::move(handler);
    }
    void SetClearOutputHandler(std::function<void()> handler)
    {
        ClearOutputHandler = std::move(handler);
    }

    [[nodiscard]] const std::vector<ConsoleCommandLine>& DeferredCommands() const
    {
        return Deferred;
    }

private:
    ConsoleResult ExecuteTokensInternal(const std::vector<std::string>& args,
                                        const ConsoleValueSource& source,
                                        bool deferrable,
                                        int execDepth);
    ConsoleResult ExecuteScriptFile(std::string_view path,
                                    const ConsoleValueSource& source,
                                    int execDepth);
    void RegisterBuiltIns();
    void FlushDeferred();

    ConsoleRegistry RegistryInstance;
    ConsolePhase CurrentPhase = ConsolePhase::EngineReady;
    std::vector<ConsoleCommandLine> Deferred;
    std::function<void()> QuitHandler;
    std::function<ConsoleResult(std::string_view)> MapHandler;
    std::function<void()> ClearOutputHandler;
    int ExecRecursionLimit = 8;
};
