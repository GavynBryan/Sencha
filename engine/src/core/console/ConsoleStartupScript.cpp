#include <core/console/ConsoleStartupScript.h>

#include <sstream>
#include <utility>

namespace
{
    std::string TrimComment(std::string_view line)
    {
        std::string result;
        result.reserve(line.size());
        bool quoted = false;
        bool escaping = false;
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (escaping)
            {
                result.push_back(c);
                escaping = false;
                continue;
            }
            if (quoted && c == '\\')
            {
                result.push_back(c);
                escaping = true;
                continue;
            }
            if (c == '"')
            {
                quoted = !quoted;
                result.push_back(c);
                continue;
            }
            if (!quoted && c == '#')
                break;
            if (!quoted && c == '/' && i + 1 < line.size() && line[i + 1] == '/')
                break;
            result.push_back(c);
        }
        return result;
    }

    std::string Join(const std::vector<std::string>& args)
    {
        std::string text;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                text += ' ';
            text += args[i];
        }
        return text;
    }
}

void ConsoleStartupScript::Append(const ConsoleStartupScript& other)
{
    Lines.insert(Lines.end(), other.Lines.begin(), other.Lines.end());
}

ConsoleStartupScript ConsoleStartupScript::FromArgv(int argc, char** argv)
{
    ConsoleStartupScript script;
    ConsoleCommandLine current;
    bool haveCommand = false;

    for (int i = 1; i < argc; ++i)
    {
        if (argv == nullptr || argv[i] == nullptr)
            continue;
        std::string token(argv[i]);
        if (token.size() > 1 && token.front() == '+')
        {
            if (haveCommand && !current.Args.empty())
            {
                current.Text = Join(current.Args);
                script.Add(std::move(current));
            }
            current = {};
            current.Source.Description = "argv";
            current.Source.Line = i;
            current.Args.push_back(token.substr(1));
            haveCommand = true;
        }
        else if (haveCommand)
        {
            current.Args.push_back(std::move(token));
        }
    }

    if (haveCommand && !current.Args.empty())
    {
        current.Text = Join(current.Args);
        script.Add(std::move(current));
    }

    return script;
}

ConsoleStartupScript ConsoleStartupScript::ParseText(
    std::string_view text,
    std::string fileName,
    std::vector<ConsoleParseDiagnostic>* diagnostics)
{
    ConsoleStartupScript script;
    std::istringstream input{ std::string(text) };
    std::string line;
    int lineNumber = 1;
    while (std::getline(input, line))
    {
        ConsoleValueSource source;
        source.Description = "script";
        source.File = fileName;
        source.Line = lineNumber;
        source.Column = 1;

        ConsoleParseDiagnostic diagnostic;
        std::vector<std::string> tokens = TokenizeLine(line, source, &diagnostic);
        if (!diagnostic.Message.empty())
        {
            if (diagnostics)
                diagnostics->push_back(std::move(diagnostic));
        }
        else if (!tokens.empty())
        {
            script.Add(ConsoleCommandLine{
                .Args = std::move(tokens),
                .Source = std::move(source),
                .Text = line,
            });
        }
        ++lineNumber;
    }
    return script;
}

std::vector<std::string> ConsoleStartupScript::TokenizeLine(
    std::string_view line,
    ConsoleValueSource source,
    ConsoleParseDiagnostic* diagnostic)
{
    const std::string trimmed = TrimComment(line);
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    bool escaping = false;
    int column = 1;

    for (std::size_t i = 0; i < trimmed.size(); ++i, ++column)
    {
        const char c = trimmed[i];
        if (escaping)
        {
            current.push_back(c);
            escaping = false;
            continue;
        }
        if (quoted && c == '\\')
        {
            escaping = true;
            continue;
        }
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t' || c == '\r'))
        {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }

    if (escaping)
        current.push_back('\\');
    if (quoted)
    {
        if (diagnostic)
        {
            source.Column = column;
            *diagnostic = { std::move(source), "unterminated quoted string" };
        }
        return {};
    }
    if (!current.empty())
        tokens.push_back(std::move(current));
    return tokens;
}
