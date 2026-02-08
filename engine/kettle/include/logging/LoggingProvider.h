#pragma once

#include <logging/Logger.h>
#include <logging/ILogSink.h>
#include <logging/LogLevel.h>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

//=============================================================================
// LoggingProvider
//
// Central factory and owner of Loggers and LogSinks. NOT an IService —
// it lives as a first-class member of ServiceHost and is accessible
// through ServiceProvider::GetLogger<T>().
//
// Usage (setup):
//   ServiceHost services;
//   services.GetLoggingProvider().AddSink<ConsoleLogSink>();
//   services.GetLoggingProvider().SetMinLevel(LogLevel::Info);
//
// Usage (consumption, typically in a system constructor):
//   RenderSystem::RenderSystem(const ServiceProvider& provider)
//       : Log(provider.GetLogger<RenderSystem>())
//   { }
//
// Loggers are created lazily and cached by type — requesting the same
// type twice returns the same Logger instance.
//=============================================================================
class LoggingProvider
{
public:
	LoggingProvider() = default;

	// Non-copyable
	LoggingProvider(const LoggingProvider&) = delete;
	LoggingProvider& operator=(const LoggingProvider&) = delete;
	LoggingProvider(LoggingProvider&&) = default;
	LoggingProvider& operator=(LoggingProvider&&) = default;

	// -- Sink management ----------------------------------------------------

	template <typename TSink, typename... Args>
	TSink& AddSink(Args&&... args)
	{
		auto sink = std::make_unique<TSink>(std::forward<Args>(args)...);
		auto* raw = sink.get();
		SinkPtrs.push_back(raw);
		OwnedSinks.emplace_back(std::move(sink));
		return *raw;
	}

	void SetMinLevel(LogLevel level)
	{
		for (auto* sink : SinkPtrs)
		{
			sink->SetMinLevel(level);
		}
	}

	// -- Logger factory -----------------------------------------------------

	template <typename T>
	Logger& GetLogger()
	{
		auto key = std::type_index(typeid(T));
		auto it = Loggers.find(key);
		if (it != Loggers.end())
		{
			return *it->second;
		}

		auto logger = std::make_unique<Logger>(CleanTypeName(typeid(T).name()), SinkPtrs);
		auto* raw = logger.get();
		Loggers[key] = std::move(logger);
		return *raw;
	}

private:
	static std::string CleanTypeName(const char* name)
	{
		std::string result(name);
		// MSVC decorates names with "class " / "struct " prefixes
		if (result.starts_with("class "))       result = result.substr(6);
		else if (result.starts_with("struct "))  result = result.substr(7);
		// GCC/MinGW mangles names with a leading length prefix (e.g. "4Game")
		// Strip leading digits to recover the clean name.
		size_t start = 0;
		while (start < result.size() && result[start] >= '0' && result[start] <= '9')
			++start;
		if (start > 0 && start < result.size())
			result = result.substr(start);
		return result;
	}

	std::vector<std::unique_ptr<ILogSink>> OwnedSinks;
	std::vector<ILogSink*> SinkPtrs;
	std::unordered_map<std::type_index, std::unique_ptr<Logger>> Loggers;
};
