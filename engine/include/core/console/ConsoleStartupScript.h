#pragma once

#include <core/console/ConsoleTypes.h>

#include <string>
#include <string_view>
#include <vector>

struct ConsoleCommandLine
{
    std::vector<std::string> Args;
    ConsoleValueSource Source;
    std::string Text;
};

struct ConsoleParseDiagnostic
{
    ConsoleValueSource Source;
    std::string Message;
};

class ConsoleStartupScript
{
public:
    void Add(ConsoleCommandLine line) { Lines.push_back(std::move(line)); }
    void Append(const ConsoleStartupScript& other);
    void Clear() { Lines.clear(); }

    [[nodiscard]] bool Empty() const { return Lines.empty(); }
    [[nodiscard]] const std::vector<ConsoleCommandLine>& Commands() const { return Lines; }
    [[nodiscard]] std::vector<ConsoleCommandLine>& Commands() { return Lines; }

    static ConsoleStartupScript FromArgv(int argc, char** argv);
    static ConsoleStartupScript ParseText(std::string_view text,
                                          std::string fileName,
                                          std::vector<ConsoleParseDiagnostic>* diagnostics = nullptr);
    static std::vector<std::string> TokenizeLine(std::string_view line,
                                                 ConsoleValueSource source,
                                                 ConsoleParseDiagnostic* diagnostic = nullptr);

private:
    std::vector<ConsoleCommandLine> Lines;
};

