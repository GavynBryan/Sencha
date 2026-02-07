#pragma once

#include <logging/LogLevel.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

//=============================================================================
// ILogSink
//
// Abstract output destination for log messages. Implementations decide
// where messages go (console, file, network, etc.) and can filter by
// minimum severity level.
//
// The LoggingProvider owns sinks and distributes messages to all of them.
//=============================================================================
class ILogSink
{
public:
	virtual ~ILogSink() = default;

	virtual void Write(LogLevel level, std::string_view category, std::string_view message) = 0;

	void SetMinLevel(LogLevel level) { MinLevel = level; }
	LogLevel GetMinLevel() const { return MinLevel; }

protected:
	LogLevel MinLevel = LogLevel::Debug;

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
