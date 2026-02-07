#pragma once

#include <logging/ILogSink.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

//=============================================================================
// FileLogSink
//
// Log sink that writes messages to a file. On construction, existing log
// files are rotated:
//
//   game.log      -> game-1.log
//   game-1.log    -> game-2.log
//   game-2.log    -> game-3.log
//   game-3.log    -> deleted
//
// This keeps at most 3 old logs plus the current one (4 files total).
// The file without a numeric suffix is always the most recent.
//
// Format:  [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] Category: message
//=============================================================================
class FileLogSink : public ILogSink
{
public:
	static constexpr int MaxOldLogs = 3;

	explicit FileLogSink(const std::string& filename)
	{
		RotateExistingLogs(filename);

		Stream.open(filename, std::ios::out | std::ios::trunc);
	}

	~FileLogSink() override
	{
		if (Stream.is_open()) Stream.close();
	}

	void Write(LogLevel level, std::string_view category, std::string_view message) override
	{
		if (level < MinLevel) return;
		if (!Stream.is_open()) return;

		Stream << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": " << message << "\n";
		Stream.flush();
	}

private:
	static void RotateExistingLogs(const std::string& basePath)
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

	static constexpr const char* LevelToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Debug:    return "DEBUG";
			case LogLevel::Info:     return "INFO";
			case LogLevel::Warning:  return "WARN";
			case LogLevel::Error:    return "ERROR";
			case LogLevel::Critical: return "CRIT";
			default:                 return "???";
		}
	}

	std::ofstream Stream;
};
