#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <chrono>

class Logger;

//=============================================================================
// RmlSystemBridge
//
// The document engine's ambient services, answered with Sencha's: a clock and
// a log sink.
//
// The clock is a monotonic wall clock started when the bridge is constructed,
// not the simulation clock. Document animation and transitions are presentation,
// and must keep running while the simulation is paused, stepped, or catching up
// on fixed ticks -- a pause menu whose own fade freezes because it paused the
// game would be absurd.
//=============================================================================
class RmlSystemBridge final : public Rml::SystemInterface
{
public:
    explicit RmlSystemBridge(Logger& log);

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    Logger& Log;
    std::chrono::steady_clock::time_point Start;
};
