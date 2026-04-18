#pragma once

#include <core/logging/ILogSink.h>
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
	void Write(LogLevel level, std::string_view category, std::string_view message) override;
};
