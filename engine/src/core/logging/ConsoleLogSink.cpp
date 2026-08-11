
#include <core/logging/ConsoleLogSink.h>

void ConsoleLogSink::Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (level < GetMinLevel()) return;

    std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
    std::lock_guard<std::mutex> lock(WriteMutex);
    // Flushed per line, because the reader is usually watching. A dedicated
    // host's terminal is its only status surface, and redirecting it to a file
    // -- which is how a server is actually run -- otherwise holds a line back
    // until a buffer fills, so "peer joined" can arrive minutes after the peer
    // did, or never, if the process is killed.
    out << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": "
        << message << std::endl;
}