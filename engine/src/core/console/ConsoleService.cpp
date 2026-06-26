#include <core/console/ConsoleService.h>

#include <fstream>
#include <sstream>

namespace
{
    ConsoleResult Usage(std::string text)
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error(std::move(text));
        return result;
    }
}

ConsoleService::ConsoleService()
{
    RegisterBuiltIns();
}

void ConsoleService::AdvancePhase(ConsolePhase phase)
{
    if (IsPhaseReady(CurrentPhase, phase))
        return;
    CurrentPhase = phase;
    FlushDeferred();
}

ConsoleResult ConsoleService::Execute(const ConsoleCommandLine& line, bool deferrable)
{
    return ExecuteTokensInternal(line.Args, line.Source, deferrable, 0);
}

ConsoleResult ConsoleService::ExecuteTokens(std::vector<std::string> args,
                                            ConsoleValueSource source,
                                            bool deferrable)
{
    return ExecuteTokensInternal(args, source, deferrable, 0);
}

ConsoleResult ConsoleService::ExecuteLine(std::string_view text,
                                          ConsoleValueSource source,
                                          bool deferrable)
{
    ConsoleParseDiagnostic diagnostic;
    std::vector<std::string> tokens =
        ConsoleStartupScript::TokenizeLine(text, source, &diagnostic);
    if (!diagnostic.Message.empty())
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error(diagnostic.Message);
        return result;
    }
    return ExecuteTokens(std::move(tokens), std::move(source), deferrable);
}

ConsoleResult ConsoleService::ExecuteStartupScript(const ConsoleStartupScript& script)
{
    ConsoleResult aggregate;
    for (const ConsoleCommandLine& line : script.Commands())
    {
        ConsoleResult result = Execute(line, true);
        aggregate.Output.insert(aggregate.Output.end(), result.Output.begin(), result.Output.end());
        if (result.Status != ConsoleStatus::Ok && result.Status != ConsoleStatus::Deferred)
            aggregate.Status = result.Status;
    }
    return aggregate;
}

ConsoleResult ConsoleService::ApplyAssignments(
    const std::vector<EngineConsoleCVarAssignment>& assignments,
    ConsoleValueSource source)
{
    ConsoleResult aggregate;
    for (const EngineConsoleCVarAssignment& assignment : assignments)
    {
        ConsoleResult result = RegistryInstance.SetCVarFromString(
            assignment.Name,
            assignment.Value,
            source,
            CurrentPhase);
        aggregate.Output.insert(aggregate.Output.end(), result.Output.begin(), result.Output.end());
        if (result.Status != ConsoleStatus::Ok && result.Status != ConsoleStatus::Deferred)
            aggregate.Status = result.Status;
    }
    return aggregate;
}

ConsoleResult ConsoleService::ExecuteTokensInternal(const std::vector<std::string>& args,
                                                    const ConsoleValueSource& source,
                                                    bool deferrable,
                                                    int execDepth)
{
    if (args.empty())
        return {};

    const std::string commandName = CanonicalConsoleName(args.front());
    const ConsoleCommandMetadata* command = RegistryInstance.FindCommand(commandName);
    if (command == nullptr)
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::UnknownCommand;
        result.Error("unknown command '" + commandName + "'");
        return result;
    }

    if (!IsPhaseReady(CurrentPhase, command->RequiredPhase))
    {
        if (deferrable)
        {
            Deferred.push_back({
                .Args = args,
                .Source = source,
                .Text = {},
            });
            ConsoleResult result;
            result.Status = ConsoleStatus::Deferred;
            result.Info("deferred '" + commandName + "' until " + ToString(command->RequiredPhase));
            return result;
        }

        ConsoleResult result;
        result.Status = ConsoleStatus::PhaseNotReady;
        result.Error("command '" + commandName + "' requires phase "
                     + ToString(command->RequiredPhase));
        return result;
    }

    if (commandName == "exec")
        return ExecuteScriptFile(args.size() > 1 ? args[1] : "", source, execDepth);

    ConsoleExecutionContext ctx{
        .Registry = RegistryInstance,
        .Phase = CurrentPhase,
        .Deferrable = deferrable,
    };
    std::span<const std::string> commandArgs(args.data() + 1, args.size() - 1);
    return command->Callback(ctx, commandArgs);
}

ConsoleResult ConsoleService::ExecuteScriptFile(std::string_view path,
                                                const ConsoleValueSource& source,
                                                int execDepth)
{
    if (path.empty())
        return Usage("usage: exec <path>");
    if (execDepth >= ExecRecursionLimit)
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::ExecutionFailed;
        result.Error("exec recursion limit reached at '" + std::string(path) + "'");
        return result;
    }

    std::ifstream file(std::string(path), std::ios::binary);
    if (!file)
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::ExecutionFailed;
        result.Error("could not open exec script '" + std::string(path) + "'");
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::vector<ConsoleParseDiagnostic> diagnostics;
    ConsoleStartupScript script =
        ConsoleStartupScript::ParseText(buffer.str(), std::string(path), &diagnostics);

    ConsoleResult aggregate;
    for (const ConsoleParseDiagnostic& diagnostic : diagnostics)
    {
        aggregate.Status = ConsoleStatus::InvalidArguments;
        aggregate.Error(diagnostic.Source.File + ":" + std::to_string(diagnostic.Source.Line)
                        + ": " + diagnostic.Message);
    }

    for (const ConsoleCommandLine& line : script.Commands())
    {
        if (!line.Args.empty() && CanonicalConsoleName(line.Args.front()) == "exec")
        {
            ConsoleResult nested =
                ExecuteScriptFile(line.Args.size() > 1 ? line.Args[1] : "", line.Source,
                                  execDepth + 1);
            aggregate.Output.insert(aggregate.Output.end(), nested.Output.begin(), nested.Output.end());
            if (!nested.Succeeded() && nested.Status != ConsoleStatus::Deferred)
                aggregate.Status = nested.Status;
        }
        else
        {
            ConsoleResult result = ExecuteTokensInternal(line.Args, line.Source, true, execDepth + 1);
            aggregate.Output.insert(aggregate.Output.end(), result.Output.begin(), result.Output.end());
            if (!result.Succeeded() && result.Status != ConsoleStatus::Deferred)
                aggregate.Status = result.Status;
        }
    }

    if (aggregate.Output.empty())
        aggregate.Info("executed " + std::string(path));
    (void)source;
    return aggregate;
}

void ConsoleService::RegisterBuiltIns()
{
    RegistryInstance.RegisterCommand({
        .Name = "echo",
        .Owner = "engine",
        .Usage = "echo <text...>",
        .Help = "Print text to the console output stream.",
        .Callback = [](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            std::string text;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                if (i > 0)
                    text += ' ';
                text += args[i];
            }
            result.Info(text);
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "set",
        .Owner = "engine",
        .Usage = "set <cvar> <value>",
        .Help = "Assign a cvar.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() < 2)
                return Usage("usage: set <cvar> <value>");
            return ctx.Registry.SetCVarFromString(args[0], args[1], { "console" }, ctx.Phase);
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "get",
        .Owner = "engine",
        .Usage = "get <cvar>",
        .Help = "Show a cvar value and metadata.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: get <cvar>");
            const CVarMetadata* cvar = ctx.Registry.FindCVar(args[0]);
            if (cvar == nullptr)
            {
                ConsoleResult result;
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("unknown cvar '" + CanonicalConsoleName(args[0]) + "'");
                return result;
            }
            ConsoleResult result;
            result.Info(cvar->Name + " = " + ToString(cvar->CurrentValue)
                        + " (default " + ToString(cvar->DefaultValue) + ")");
            if (cvar->LatchedValue)
                result.Info("latched = " + ToString(*cvar->LatchedValue));
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "toggle",
        .Owner = "engine",
        .Usage = "toggle <bool-cvar>",
        .Help = "Flip a boolean cvar.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: toggle <bool-cvar>");
            const CVarMetadata* cvar = ctx.Registry.FindCVar(args[0]);
            if (cvar == nullptr || cvar->Type != CVarType::Bool)
                return Usage("toggle requires a boolean cvar");
            return ctx.Registry.SetCVar(cvar->Name, !std::get<bool>(cvar->CurrentValue),
                                        { "toggle" }, ctx.Phase);
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "reset",
        .Owner = "engine",
        .Usage = "reset <cvar>",
        .Help = "Reset one cvar to its default.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: reset <cvar>");
            return ctx.Registry.ResetCVar(args[0], ctx.Phase);
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "reset_tree",
        .Owner = "engine",
        .Usage = "reset_tree <prefix>",
        .Help = "Reset every cvar under a dotted namespace prefix.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: reset_tree <prefix>");
            return ctx.Registry.ResetTree(args[0], ctx.Phase);
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "list",
        .Owner = "engine",
        .Usage = "list [prefix]",
        .Help = "List commands and cvars.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            const std::string prefix = args.empty() ? std::string{} : args[0];
            ConsoleResult result;
            for (const CVarMetadata* cvar : ctx.Registry.ListCVars(prefix))
                result.Info("cvar " + cvar->Name + " = " + ToString(cvar->CurrentValue));
            for (const ConsoleCommandMetadata* command : ctx.Registry.ListCommands(prefix))
                result.Info("cmd  " + command->Name + " " + command->Usage);
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "find",
        .Owner = "engine",
        .Usage = "find <prefix>",
        .Help = "Autocomplete names by prefix.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: find <prefix>");
            ConsoleResult result;
            for (const std::string& match : ctx.Registry.Complete(args[0]))
                result.Info(match);
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "help",
        .Owner = "engine",
        .Usage = "help [name]",
        .Help = "Show command or cvar help.",
        .Callback = [](ConsoleExecutionContext& ctx, std::span<const std::string> args) {
            ConsoleResult result;
            if (args.empty())
            {
                result.Info("Use list, find <prefix>, get <cvar>, set <cvar> <value>.");
                return result;
            }
            const std::string name = CanonicalConsoleName(args[0]);
            if (const CVarMetadata* cvar = ctx.Registry.FindCVar(name))
            {
                result.Info(cvar->Name + ": " + cvar->Help);
                return result;
            }
            if (const ConsoleCommandMetadata* command = ctx.Registry.FindCommand(name))
            {
                result.Info(command->Name + " - " + command->Help);
                result.Info("usage: " + command->Usage);
                for (const std::string& example : command->Examples)
                    result.Info("example: " + example);
                return result;
            }
            result.Status = ConsoleStatus::UnknownCommand;
            result.Error("no help for '" + name + "'");
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "map",
        .Owner = "engine",
        .Usage = "map <path>",
        .Help = "Request loading a startup/play map through the host-provided map handler.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: map <path>");
            if (!MapHandler)
            {
                ConsoleResult result;
                result.Status = ConsoleStatus::ExecutionFailed;
                result.Error("map command has no host handler");
                return result;
            }
            return MapHandler(args[0]);
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "exec",
        .Owner = "engine",
        .Usage = "exec <path>",
        .Help = "Execute a console script file.",
        .Callback = [](ConsoleExecutionContext&, std::span<const std::string>) -> ConsoleResult {
            return ConsoleResult{};
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "clear",
        .Owner = "engine",
        .Usage = "clear",
        .Help = "Clear frontend console output.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
            if (ClearOutputHandler)
                ClearOutputHandler();
            ConsoleResult result;
            result.Info("cleared");
            return result;
        },
    });

    RegistryInstance.RegisterCommand({
        .Name = "quit",
        .Owner = "engine",
        .Usage = "quit",
        .Help = "Request application exit.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
            if (QuitHandler)
                QuitHandler();
            ConsoleResult result;
            result.Info("quit requested");
            return result;
        },
    });
}

void ConsoleService::FlushDeferred()
{
    std::vector<ConsoleCommandLine> remaining;
    remaining.reserve(Deferred.size());
    for (const ConsoleCommandLine& line : Deferred)
    {
        const ConsoleCommandMetadata* command =
            line.Args.empty() ? nullptr : RegistryInstance.FindCommand(line.Args.front());
        if (command != nullptr && IsPhaseReady(CurrentPhase, command->RequiredPhase))
            (void)Execute(line, true);
        else
            remaining.push_back(line);
    }
    Deferred = std::move(remaining);
}
