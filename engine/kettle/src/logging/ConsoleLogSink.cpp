
#include <logging/ConsoleLogSink.h>

void ConsoleLogSink::Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (level < MinLevel) return;

    std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
    out << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": " << message << "\n";
}