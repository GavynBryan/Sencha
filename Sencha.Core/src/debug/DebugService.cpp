#include <debug/DebugService.h>

DebugService::DebugService(LoggingProvider& logging, DebugLogSink& sink)
	: Log(logging.GetLogger<DebugService>())
	, LogSink(sink)
{
	Log.Info("Debug service ready");
}

DebugService::~DebugService() = default;

void DebugService::Toggle()
{
	if (Opened)
		Close();
	else
		Open();
}

void DebugService::Open()
{
	if (Opened)
		return;

	Opened = true;
	Log.Debug("Debug opened");
}

void DebugService::Close()
{
	if (!Opened)
		return;

	Opened = false;
	Log.Debug("Debug closed");
}
