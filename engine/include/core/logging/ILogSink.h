#pragma once

#include <core/logging/LogLevel.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

//=============================================================================
//LevelToString
//static helper to convert LogLevel enum to string for output formatting.
//=============================================================================
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

//=============================================================================
// ILogSink
//
// Abstract output destination for log messages. Implementations decide
// where messages go (console, file, network, etc.) and can filter by
// minimum severity level.
//
// The LoggingProvider owns sinks and distributes messages to all of them.
//
// Threading: Write must be safe to call from job/task threads (see
// docs/ecs/parallelization.md). Sink construction and LoggingProvider setup
// remain main-thread-only; MinLevel is atomic so a setup-time SetMinLevel
// never races an in-flight Write.
//=============================================================================
class ILogSink
{
public:
	virtual ~ILogSink() = default;

	virtual void Write(LogLevel level, std::string_view category, std::string_view message) = 0;

	void SetMinLevel(LogLevel level) { MinLevel.store(level, std::memory_order_relaxed); }
	LogLevel GetMinLevel() const { return MinLevel.load(std::memory_order_relaxed); }

protected:
	std::atomic<LogLevel> MinLevel{ LogLevel::Debug };

	// Returns a timestamp string: "YYYY-MM-DD HH:MM:SS.mmm"
	static std::string Timestamp()
	{
		using namespace std::chrono;
		auto now   = system_clock::now();
		auto timeT = system_clock::to_time_t(now);
		auto ms    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

		std::tm buf{};
#ifdef _WIN32
		localtime_s(&buf, &timeT);
#else
		localtime_r(&timeT, &buf);
#endif

		std::ostringstream oss;
		oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S")
		    << '.' << std::setfill('0') << std::setw(3) << ms.count();
		return oss.str();
	}
};
