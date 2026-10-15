#include "OperationSocket.h"

#include "EditorOperations.h"
#include "OperationScript.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <optional>

namespace
{
    JsonValue Malformed(std::string message)
    {
        return JsonValue(JsonValue::Object{
            { "status", JsonValue("error") },
            { "code", JsonValue("request.malformed") },
            { "message", JsonValue(std::move(message)) },
        });
    }

    // A JSON request's answer, its id carried back.
    JsonValue AnswerRequest(std::string_view text, EditorOperations& operations)
    {
        const std::optional<JsonValue> request = JsonParse(text);
        if (!request || !request->IsObject())
            return Malformed("a request is a JSON object, or a line of the script form");
        const JsonValue* operation = request->Find("op");
        const JsonValue* arguments = request->Find("args");
        JsonValue answer = operation != nullptr && operation->IsString()
            ? operations.Invoke(operation->AsString(),
                                arguments != nullptr ? *arguments : JsonValue(JsonValue::Object{}))
            : Malformed("a request names its operation in \"op\"");
        if (const JsonValue* id = request->Find("id"); id != nullptr && answer.IsObject())
            answer.AsObject().insert(answer.AsObject().begin(), { "id", *id });
        return answer;
    }
}

OperationSocket::OperationSocket(EditorOperations& operations)
    : Operations(operations)
{
    Listener = Operations.Listen([this](const JsonValue& event) {
        if (Server.IsOpen())
            Server.Broadcast(JsonStringify(event));
    });
}

OperationSocket::~OperationSocket()
{
    Operations.StopListening(Listener);
}

bool OperationSocket::Open(const std::filesystem::path& path, std::string& error)
{
    return Server.Open(path, error);
}

void OperationSocket::Poll()
{
    for (const ReceivedLine& line : Server.Poll())
    {
        const std::size_t start = line.Text.find_first_not_of(" \t");
        const std::optional<JsonValue> answer = start != std::string::npos && line.Text[start] == '{'
            ? std::optional<JsonValue>(AnswerRequest(line.Text, Operations))
            : AnswerOperationLine(line.Text, Operations);
        if (answer)
            Server.Send(line.Client, JsonStringify(*answer));
    }
}
