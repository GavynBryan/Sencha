#pragma once

#include <core/logging/LogLevel.h>
#include <string>

//=============================================================================
// DebugLogEntry
//
// A single captured log message held in the DebugLogSink ring buffer.
// Stores the level, source category, and message text so the console panel
// can filter and display them.
//=============================================================================
struct DebugLogEntry
{
	LogLevel    Level;
	std::string Category;
	std::string Message;
};
