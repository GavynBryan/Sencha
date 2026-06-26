#include <core/console/ConsoleCompletion.h>

#include <core/console/ConsoleRegistry.h>

namespace
{
    constexpr bool IsWhitespace(char c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }
}

std::vector<std::string> SuggestConsoleCompletions(const ConsoleRegistry& registry,
                                                   std::string_view line)
{
    // Split on whitespace to find which token the cursor sits in and its prefix.
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && IsWhitespace(line[i]))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !IsWhitespace(line[i]))
            ++i;
        if (i > start)
            tokens.push_back(line.substr(start, i - start));
    }

    // A trailing space (or an empty line) means a fresh token is being started,
    // so the prefix is empty and the index is one past the completed tokens.
    const bool startingNewToken = line.empty() || IsWhitespace(line.back());
    const std::size_t tokenIndex = startingNewToken ? tokens.size() : tokens.size() - 1;
    const std::string_view prefix = startingNewToken ? std::string_view{} : tokens.back();

    std::vector<std::string> result;
    if (tokenIndex == 0)
    {
        for (const ConsoleCommandMetadata* command : registry.ListCommands(prefix))
            result.push_back(command->Name);
    }
    else if (tokenIndex == 1)
    {
        for (const CVarMetadata* cvar : registry.ListCVars(prefix))
            result.push_back(cvar->Name);
    }
    return result;
}
