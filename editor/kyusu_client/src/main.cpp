#include "ipc/LineSocket.h"

#include <core/json/JsonParser.h>

#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    constexpr int kAnswersHadErrors = 1;
    constexpr int kCouldNotConnect = 2;

    constexpr std::string_view kUsage =
        "usage: kyusu_client [--socket <path>] [--timeout <ms>] [<operation> [key=value ...]]\n"
        "       kyusu_client [--socket <path>] --events [<count>]\n"
        "Sends one operation to a running Kyusu, or each line of stdin, and prints each answer as\n"
        "one line of JSON. --events prints what the editor reports as it happens. Kyusu listens\n"
        "while editor.operations.socket is on.\n";

    struct Options
    {
        std::string Socket;
        int TimeoutMs = 30000;
        bool Events = false;
        long EventCount = -1;
        std::string Operation;
    };

    bool ParseOptions(int argc, char** argv, Options& options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view arg = argv[index];
            if (!options.Operation.empty())
                options.Operation += " " + std::string(arg);
            else if (arg == "--socket" && index + 1 < argc)
                options.Socket = argv[++index];
            else if (arg == "--timeout" && index + 1 < argc)
                options.TimeoutMs = std::atoi(argv[++index]);
            else if (arg == "--events")
            {
                options.Events = true;
                if (index + 1 < argc && std::string_view(argv[index + 1]).find_first_not_of("0123456789") == std::string_view::npos)
                    options.EventCount = std::atol(argv[++index]);
            }
            else if (arg.starts_with("--"))
                return false;
            else
                options.Operation = std::string(arg);
        }
        return !(options.Events && !options.Operation.empty());
    }

    bool IsEvent(const std::string& line)
    {
        const std::optional<JsonValue> parsed = JsonParse(line);
        return parsed && parsed->IsObject() && parsed->Find("event") != nullptr;
    }

    bool IsError(const std::string& line)
    {
        const std::optional<JsonValue> parsed = JsonParse(line);
        const JsonValue* status = parsed && parsed->IsObject() ? parsed->Find("status") : nullptr;
        return status == nullptr || !status->IsString() || status->AsString() == "error";
    }

    // Sends `request` and prints its answer, passing over events on the way.
    // False when no answer came or the answer was an error.
    bool Ask(LineSocketClient& client, const std::string& request, int timeoutMs)
    {
        if (!client.Send(request))
        {
            std::fputs("kyusu_client: the editor closed the connection\n", stderr);
            return false;
        }
        while (const std::optional<std::string> line = client.Receive(timeoutMs))
        {
            if (IsEvent(*line))
                continue;
            std::cout << *line << '\n';
            return !IsError(*line);
        }
        std::fputs("kyusu_client: no answer\n", stderr);
        return false;
    }
}

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fputs(kUsage.data(), stderr);
        return kCouldNotConnect;
    }

    LineSocketClient client;
    std::string error;
    if (!client.Connect(options.Socket.empty() ? DefaultEditorSocketPath() : std::filesystem::path(options.Socket), error))
    {
        std::fprintf(stderr, "kyusu_client: %s; is editor.operations.socket on?\n", error.c_str());
        return kCouldNotConnect;
    }

    if (options.Events)
    {
        for (long seen = 0; options.EventCount < 0 || seen < options.EventCount;)
            if (const std::optional<std::string> line = client.Receive(-1); line && IsEvent(*line))
            {
                std::cout << *line << std::endl;
                ++seen;
            }
            else if (!line)
                break;
        return 0;
    }

    if (!options.Operation.empty())
        return Ask(client, options.Operation, options.TimeoutMs) ? 0 : kAnswersHadErrors;

    bool errors = false;
    for (std::string line; std::getline(std::cin, line);)
    {
        const std::size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#')
            continue;
        errors = !Ask(client, line, options.TimeoutMs) || errors;
    }
    return errors ? kAnswersHadErrors : 0;
}
