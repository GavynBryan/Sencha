#include <logging/FileLogSink.h>

FileLogSink::FileLogSink(const std::string& filename)
{
    RotateExistingLogs(filename);

    Stream.open(filename, std::ios::out | std::ios::trunc);
}

FileLogSink::~FileLogSink()
{
	if (Stream.is_open()) Stream.close();
}

void FileLogSink::Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (level < MinLevel) return;
    if (!Stream.is_open()) return;

    Stream << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": " << message << "\n";
    Stream.flush();
}

void FileLogSink::RotateExistingLogs(const std::string& basePath)
{
    namespace fs = std::filesystem;

    auto stem = fs::path(basePath).stem().string();
    auto ext  = fs::path(basePath).extension().string();
    auto dir  = fs::path(basePath).parent_path();

    auto numberedPath = [&](int n) -> fs::path {
        return dir / (stem + "-" + std::to_string(n) + ext);
    };

    // Delete the oldest if it exists
    fs::path oldest = numberedPath(MaxOldLogs);
    if (fs::exists(oldest))
    {
        fs::remove(oldest);
    }

    // Shift numbered logs up: -2 -> -3, -1 -> -2
    for (int i = MaxOldLogs - 1; i >= 1; --i)
    {
        fs::path src = numberedPath(i);
        fs::path dst = numberedPath(i + 1);
        if (fs::exists(src))
        {
            fs::rename(src, dst);
        }
    }

    // Rotate the current log to -1
    if (fs::exists(basePath))
    {
        fs::rename(basePath, numberedPath(1));
    }
}