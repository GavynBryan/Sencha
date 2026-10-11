#include "EditorOperations.h"

#include "OperationArguments.h"

#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonStringify.h>

#include <format>

namespace
{
    // Console commands live under a name of their own so retiring a provider's
    // operations never unregisters that provider's other commands.
    std::string CommandOwner(std::string_view provider)
    {
        return std::format("operations:{}", provider);
    }

    std::string_view StatusText(EditorOperationStatus status)
    {
        switch (status)
        {
        case EditorOperationStatus::Ok:
            return "ok";
        case EditorOperationStatus::Refused:
            return "refused";
        case EditorOperationStatus::Error:
            return "error";
        }
        return "error";
    }

    JsonValue Problems(std::span<const DataValidationError> errors)
    {
        JsonValue::Array problems;
        for (const DataValidationError& error : errors)
        {
            problems.push_back(JsonValue(JsonValue::Object{
                { "path", JsonValue(error.Path) },
                { "message", JsonValue(error.Message) },
            }));
        }
        return JsonValue(std::move(problems));
    }

    JsonValue Answer(std::string_view operation,
                     EditorOperationStatus status,
                     std::string_view code,
                     std::string_view message,
                     std::span<const DataValidationError> problems,
                     JsonValue data)
    {
        JsonValue::Object answer{
            { "operation", JsonValue(std::string(operation)) },
            { "status", JsonValue(std::string(StatusText(status))) },
        };
        if (!code.empty())
            answer.emplace_back("code", JsonValue(std::string(code)));
        if (!message.empty())
            answer.emplace_back("message", JsonValue(std::string(message)));
        if (!problems.empty())
            answer.emplace_back("problems", Problems(problems));
        if (status == EditorOperationStatus::Ok)
            answer.emplace_back("data", std::move(data));
        return JsonValue(std::move(answer));
    }
}

EditorOperationOutcome EditorOperationOutcome::Ok(JsonValue data)
{
    EditorOperationOutcome outcome;
    outcome.Data = std::move(data);
    return outcome;
}

EditorOperationOutcome EditorOperationOutcome::Refused(std::string code, std::string message)
{
    EditorOperationOutcome outcome;
    outcome.Status = EditorOperationStatus::Refused;
    outcome.Code = std::move(code);
    outcome.Message = std::move(message);
    return outcome;
}

bool EditorOperations::Register(std::string provider,
                                std::vector<EditorOperationEntry> entries,
                                std::vector<std::string>& errors)
{
    const std::size_t errorsBefore = errors.size();
    AuthoredRegistrationScope<EditorOperationCatalogTraits> scope(Catalog_, provider);
    std::vector<std::string> names;
    for (EditorOperationEntry& entry : entries)
    {
        if (!entry.Execute)
            errors.push_back(std::format("'{}' has no executor", entry.Definition.Name));
        names.push_back(entry.Definition.Name);
        (void)scope.Declare(entry.Definition);
    }
    if (errors.size() == errorsBefore && scope.Commit())
    {
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const EditorOperationId id = Catalog_.Find(names[index]);
            const std::size_t slot = EditorOperationCatalog::IndexOf(id);
            if (Executors.size() <= slot)
                Executors.resize(slot + 1);
            Executors[slot] = std::move(entries[index].Execute);
            RegisterCommand(id);
        }
        return true;
    }
    errors.insert(errors.end(), scope.Errors().begin(), scope.Errors().end());
    return false;
}

void EditorOperations::Retire(std::string_view provider)
{
    for (const EditorOperationId id : Catalog_.Live())
    {
        if (Catalog_.Provider(id) == provider)
            Executors[EditorOperationCatalog::IndexOf(id)] = nullptr;
    }
    Catalog_.RetireProvider(provider);
    if (Console != nullptr)
        Console->UnregisterOwner(CommandOwner(provider));
}

JsonValue EditorOperations::Invoke(std::string_view name, const JsonValue& arguments)
{
    const EditorOperationId id = Catalog_.Find(name);
    if (!id.IsValid())
        return Answer(name, EditorOperationStatus::Error, "operation.unknown",
                      std::format("no operation is named '{}'", name), {}, {});
    const EditorOperationDefinition& definition = *Catalog_.Get(id);

    DataValueRules parameterRules;
    parameterRules.FillDefaults = true;
    JsonValue checked;
    std::vector<DataValidationError> problems;
    if (!NormalizeDataValue(&arguments, definition.Parameters, "$", parameterRules, &checked,
                            problems))
        return Answer(name, EditorOperationStatus::Error, "operation.invalid_arguments",
                      "the arguments do not satisfy the operation's parameters", problems, {});

    EditorOperationOutcome outcome = Executors[EditorOperationCatalog::IndexOf(id)](checked);
    if (outcome.Status != EditorOperationStatus::Ok)
        return Answer(name, outcome.Status, outcome.Code, outcome.Message, {}, {});

    // A client reads answers by their declared shape; an answer off it is the
    // operation's defect, reported as such rather than handed on.
    if (!NormalizeDataValue(&outcome.Data, definition.Result, "$", DataValueRules{}, nullptr,
                            problems))
        return Answer(name, EditorOperationStatus::Error, "operation.result_contract",
                      "the operation answered outside its declared result", problems, {});

    return Answer(name, EditorOperationStatus::Ok, {}, {}, {}, std::move(outcome.Data));
}

void EditorOperations::ExposeTo(ConsoleRegistry& console)
{
    Console = &console;
    for (const EditorOperationId id : Catalog_.Live())
        RegisterCommand(id);
}

void EditorOperations::RegisterCommand(EditorOperationId id)
{
    if (Console == nullptr)
        return;
    const EditorOperationDefinition& definition = *Catalog_.Get(id);
    (void)Console->RegisterCommand({
        .Name = definition.Name,
        .Owner = CommandOwner(Catalog_.Provider(id)),
        .Usage = definition.Name + " [{json5 arguments} | name=value ...]",
        .Help = definition.Description,
        .Callback = [this, name = definition.Name](ConsoleExecutionContext&,
                                                   std::span<const std::string> tokens) {
            ConsoleResult result;
            std::string error;
            const std::optional<JsonValue> arguments = OperationArgumentsFromTokens(tokens, error);
            if (!arguments)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error(error);
                return result;
            }
            const JsonValue answer = Invoke(name, *arguments);
            const JsonValue* status = answer.Find("status");
            if (status == nullptr || status->AsString() == "error")
                result.Status = ConsoleStatus::ExecutionFailed;
            result.Info(JsonStringify(answer));
            return result;
        },
    });
}
