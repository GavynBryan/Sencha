
#include <core/logging/ConsoleLogSink.h>

void ConsoleLogSink::Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (level < GetMinLevel()) return;

    std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
    std::lock_guard<std::mutex> lock(WriteMutex);
    out << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": " << message << "\n";
}