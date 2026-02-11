#pragma once

#include <kettle/logging/ILogSink.h>
#include <iostream>
#include <string_view>

//=============================================================================
// ConsoleLogSink
//
// Default log sink that writes to stdout (Debug/Info/Warning) or
// stderr (Error/Critical). Messages below MinLevel are suppressed.
//
// Format:  [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] Category: message
//=============================================================================
class ConsoleLogSink : public ILogSink
{
public:
	void Write(LogLevel level, std::string_view category, std::string_view message) override
	{
		if (level < MinLevel) return;

		std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
		out << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << category << ": " << message << "\n";
	}

private:
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
};
