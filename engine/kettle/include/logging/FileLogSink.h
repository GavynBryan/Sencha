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

	explicit FileLogSink(const std::string& filename);

	~FileLogSink() override;

	void Write(LogLevel level, std::string_view category, std::string_view message) override;

private:
	static void RotateExistingLogs(const std::string& basePath);
	std::ofstream Stream;
};
