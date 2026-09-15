#include "RmlSystemBridge.h"

#include <core/logging/Logger.h>

RmlSystemBridge::RmlSystemBridge(Logger& log)
    : Log(log)
    , Start(std::chrono::steady_clock::now())
{
}

double RmlSystemBridge::GetElapsedTime()
{
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - Start;
    return elapsed.count();
}

bool RmlSystemBridge::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
    switch (type)
    {
    case Rml::Log::LT_ALWAYS:
    case Rml::Log::LT_ERROR:
    case Rml::Log::LT_ASSERT:
        Log.Error("ui: {}", message);
        break;
    case Rml::Log::LT_WARNING:
        Log.Warn("ui: {}", message);
        break;
    case Rml::Log::LT_INFO:
        Log.Info("ui: {}", message);
        break;
    case Rml::Log::LT_DEBUG:
    case Rml::Log::LT_MAX:
    default:
        Log.Debug("ui: {}", message);
        break;
    }

    // False would abort the process on an assert-level message. An authoring
    // mistake in a document is a diagnostic, never a reason to take the game
    // down with it.
    return true;
}
