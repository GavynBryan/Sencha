#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>

//=============================================================================
// DebugService
//
// Backend state for engine debugging. This service deliberately knows nothing
// about SDL, ImGui, Vulkan, render phases, panels, or game pause policy.
//
// Logging integration:
//   A DebugLogSink should be registered with LoggingProvider before loggers are
//   created, then passed here so debug frontends can inspect captured output.
//=============================================================================
class DebugService : public IService
{
public:
	// sink must already be registered with LoggingProvider via AddSink<DebugLogSink>()
	// before constructing DebugService, so it captures log output from startup.
	DebugService(LoggingProvider& logging, DebugLogSink& sink);

	~DebugService() override;

	DebugService(const DebugService&) = delete;
	DebugService& operator=(const DebugService&) = delete;
	DebugService(DebugService&&) = delete;
	DebugService& operator=(DebugService&&) = delete;

	// -- Toggle ---------------------------------------------------------------

	void Toggle();
	void Open();
	void Close();
	bool IsOpen() const { return Opened; }

	// -- Sink access ----------------------------------------------------------

	DebugLogSink& GetLogSink() { return LogSink; }
	const DebugLogSink& GetLogSink() const { return LogSink; }

private:
	Logger& Log;
	DebugLogSink& LogSink;
	bool Opened = false;
};
