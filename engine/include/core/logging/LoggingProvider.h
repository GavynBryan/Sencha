#pragma once

#include <core/logging/Logger.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LogLevel.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#define SENCHA_HAS_CXA_DEMANGLE 1
#include <cxxabi.h>
#endif
#endif

//=============================================================================
// LoggingProvider
//
// Central factory and owner of Loggers and LogSinks. NOT an IService â€”
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
// Loggers are created lazily and cached by type â€” requesting the same
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

	void Clear()
	{
		Loggers.clear();
		SinkPtrs.clear();
		OwnedSinks.clear();
	}

	// -- Logger factory -----------------------------------------------------

	// Returns a logger for the specified type T.
	// Resolves from cache or creates a new one if it doesn't exist. 
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
	// Helper to clean up type names for logger categories.
	// Strips common compiler-specific prefixes and anonymous namespace noise.
	static std::string CleanTypeName(const char* name)
	{
		std::string result = DemangleTypeName(name);
		StripTypePrefix(result);
		StripAnonymousNamespaceQualifier(result);

		if (auto component = TryGetLastItaniumComponent(result); !component.empty())
		{
			result = component;
		}

		// GCC/MinGW can expose simple names with a leading length prefix
		// (e.g. "4Game") when demangling is unavailable.
		size_t start = 0;
		while (start < result.size() && result[start] >= '0' && result[start] <= '9')
		{
			++start;
		}
		if (start > 0 && start < result.size())
		{
			result = result.substr(start);
		}
		return result;
	}

	static std::string DemangleTypeName(const char* name)
	{
#if defined(SENCHA_HAS_CXA_DEMANGLE)
		int status = 0;
		char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
		if (status == 0 && demangled)
		{
			std::string result(demangled);
			std::free(demangled);
			return result;
		}

		std::free(demangled);
#endif
		return std::string(name);
	}

	static void StripTypePrefix(std::string& name)
	{
		static constexpr std::string_view Prefixes[] = {
			"class ",
			"struct ",
			"enum ",
			"union ",
		};

		for (auto prefix : Prefixes)
		{
			if (name.starts_with(prefix))
			{
				name = name.substr(prefix.size());
				return;
			}
		}
	}

	static void StripAnonymousNamespaceQualifier(std::string& name)
	{
		static constexpr std::string_view Qualifiers[] = {
			"(anonymous namespace)::",
			"`anonymous namespace'::",
			"{anonymous}::",
		};

		for (auto qualifier : Qualifiers)
		{
			size_t pos = 0;
			while ((pos = name.find(qualifier, pos)) != std::string::npos)
			{
				name.erase(pos, qualifier.size());
			}
		}
	}

	static std::string TryGetLastItaniumComponent(std::string_view name)
	{
		if (name.size() < 3 || name.front() != 'N' || name.back() != 'E')
		{
			return {};
		}

		std::string_view lastComponent;
		size_t pos = 1;
		const size_t end = name.size() - 1;
		while (pos < end)
		{
			if (name[pos] < '0' || name[pos] > '9')
			{
				return {};
			}

			size_t length = 0;
			while (pos < end && name[pos] >= '0' && name[pos] <= '9')
			{
				length = length * 10 + static_cast<size_t>(name[pos] - '0');
				++pos;
			}

			if (length == 0 || pos + length > end)
			{
				return {};
			}

			lastComponent = name.substr(pos, length);
			pos += length;
		}

		return std::string(lastComponent);
	}

	std::vector<std::unique_ptr<ILogSink>> OwnedSinks;
	std::vector<ILogSink*> SinkPtrs;
	std::unordered_map<std::type_index, std::unique_ptr<Logger>> Loggers;
};
