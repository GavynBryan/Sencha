#include "OperationScript.h"

#include "EditorOperations.h"
#include "OperationArguments.h"

#include <core/json/JsonStringify.h>

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace
{
    JsonValue MalformedLine(std::string_view operation, std::string message)
    {
        return JsonValue(JsonValue::Object{
            { "operation", JsonValue(std::string(operation)) },
            { "status", JsonValue("error") },
            { "code", JsonValue("script.malformed") },
            { "message", JsonValue(std::move(message)) },
        });
    }
}

std::optional<JsonValue> AnswerOperationLine(std::string_view line, EditorOperations& operations)
{
    const std::size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string_view::npos || line[start] == '#')
        return std::nullopt;
    const std::size_t nameEnd = line.find_first_of(" \t\r", start);
    const std::string_view name =
        line.substr(start, nameEnd == std::string_view::npos ? std::string_view::npos : nameEnd - start);
    const std::string_view rest = nameEnd == std::string_view::npos ? std::string_view{} : line.substr(nameEnd);

    std::string error;
    const std::optional<JsonValue> arguments = OperationArgumentsFromText(rest, error);
    return arguments ? operations.Invoke(name, *arguments) : MalformedLine(name, std::move(error));
}

std::size_t RunOperationScript(std::istream& script, std::ostream& answers,
                               EditorOperations& operations)
{
    std::size_t errors = 0;
    std::string line;
    while (std::getline(script, line))
    {
        const std::optional<JsonValue> answer = AnswerOperationLine(line, operations);
        if (!answer)
            continue;
        const JsonValue* status = answer->Find("status");
        if (status != nullptr && status->IsString() && status->AsString() == "error")
            ++errors;
        answers << JsonStringify(*answer) << '\n';
    }
    answers.flush();
    return errors;
}
