#include "UiDiagnosticLog.h"

#include <utility>

UiDiagnosticLog::Scope::Scope(UiDiagnosticLog& log, Attribution attribution)
    : Log(log)
    , Previous(log.Active)
{
    Log.Active = attribution;
}

UiDiagnosticLog::Scope::~Scope()
{
    Log.Active = Previous;
}

void UiDiagnosticLog::Scope::Set(Attribution attribution)
{
    Log.Active = attribution;
}

UiDiagnosticLog::UiDiagnosticLog(Logger& log, std::size_t capacity)
    : Sink(log)
    , Capacity(capacity)
{
}

void UiDiagnosticLog::Push(UiDiagnostic entry)
{
    entry.Sequence = NextSequence++;
    if (!entry.Surface.IsValid())
        entry.Surface = Active.Surface;
    if (!entry.Screen.IsValid())
        entry.Screen = Active.Screen;

    switch (entry.Severity)
    {
    case UiDiagnosticSeverity::Error:   Sink.Error(entry.Message); break;
    case UiDiagnosticSeverity::Warning: Sink.Warn(entry.Message); break;
    case UiDiagnosticSeverity::Info:
    default:                            Sink.Info(entry.Message); break;
    }

    if (Ring.size() >= Capacity)
        Ring.pop_front();
    Ring.push_back(std::move(entry));
}

std::vector<UiDiagnostic> UiDiagnosticLog::Drain()
{
    std::vector<UiDiagnostic> out(Ring.begin(), Ring.end());
    Ring.clear();
    return out;
}
