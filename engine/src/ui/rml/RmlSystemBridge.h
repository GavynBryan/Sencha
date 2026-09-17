#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <chrono>

class UiDiagnosticLog;

//=============================================================================
// RmlSystemBridge
//
// The document engine's ambient services, answered with Sencha's: a clock and
// a diagnostics sink.
//
// The clock is a monotonic wall clock started when the bridge is constructed,
// not the simulation clock. Document animation and transitions are presentation,
// and must keep running while the simulation is paused, stepped, or catching up
// on fixed ticks -- a pause menu whose own fade freezes because it paused the
// game would be absurd.
//
// The log goes through UiDiagnosticLog rather than straight to a Logger so a
// host can read what the engine reported as data. This is also the one place
// that knows the engine's wording: the three messages it emits for a binding
// the model lacks are recognised here, and nowhere else, so an upgrade that
// rewords them fails a test in this layer rather than a tool downstream.
//=============================================================================
class RmlSystemBridge final : public Rml::SystemInterface
{
public:
    explicit RmlSystemBridge(UiDiagnosticLog& diagnostics);

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    UiDiagnosticLog& Diagnostics;
    std::chrono::steady_clock::time_point Start;
};
