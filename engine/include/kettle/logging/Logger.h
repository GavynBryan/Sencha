#pragma once

#include <kettle/logging/LogLevel.h>
#include <kettle/logging/ILogSink.h>
#include <format>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Logger
//
// A named logger instance that forwards messages to a set of sinks.
// Each Logger carries a category string (typically the class name) so
// that log output can be filtered or searched by origin.
//
// Obtained via LoggingProvider::GetLogger<T>() or
//              ServiceProvider::GetLogger<T>().
//
// Supports std::format-style formatting:
//   logger.Info("Loaded {} textures in {:.1f}ms", count, elapsed);
//=============================================================================
class Logger
{
public:
	Logger(std::string category, const std::vector<ILogSink*>& sinks)
		: Category(std::move(category)), Sinks(sinks) {}

	void Log(LogLevel level, std::string_view message) const
	{
		for (auto* sink : Sinks)
		{
			sink->Write(level, Category, message);
		}
	}

	// -- Convenience: plain string overloads --------------------------------

	void Debug(std::string_view message)       const { Log(LogLevel::Debug, message); }
	void Info(std::string_view message)        const { Log(LogLevel::Info, message); }
	void Warn(std::string_view message)        const { Log(LogLevel::Warning, message); }
	void Error(std::string_view message)       const { Log(LogLevel::Error, message); }
	void Critical(std::string_view message)    const { Log(LogLevel::Critical, message); }

	// -- Convenience: std::format overloads ---------------------------------

	template <typename... Args>
	void Debug(std::format_string<Args...> fmt, Args&&... args) const
	{
		Log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Info(std::format_string<Args...> fmt, Args&&... args) const
	{
		Log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Warn(std::format_string<Args...> fmt, Args&&... args) const
	{
		Log(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Error(std::format_string<Args...> fmt, Args&&... args) const
	{
		Log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Critical(std::format_string<Args...> fmt, Args&&... args) const
	{
		Log(LogLevel::Critical, std::format(fmt, std::forward<Args>(args)...));
	}

	const std::string& GetCategory() const { return Category; }

private:
	std::string Category;
	std::vector<ILogSink*> Sinks;
};
