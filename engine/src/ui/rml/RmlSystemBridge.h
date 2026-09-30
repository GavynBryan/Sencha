#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <SDL3/SDL_mouse.h>

#include <array>
#include <chrono>

class UiDiagnosticLog;

//=============================================================================
// RmlSystemBridge
//
// The document engine's ambient services, answered with Sencha's: a clock, a
// diagnostics sink, the clipboard and the pointer's shape.
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

    ~RmlSystemBridge() override;

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    // The platform's clipboard, so a field copies to and pastes from the rest
    // of the desktop. Nothing happens in a process with no video subsystem.
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;

    // A document's `cursor` property, as the platform's own cursor shapes. A
    // name the platform has no shape for shows the default arrow.
    void SetMouseCursor(const Rml::String& cursorName) override;

private:
    UiDiagnosticLog& Diagnostics;
    std::chrono::steady_clock::time_point Start;
    // Created on first use and kept, indexed by SDL_SystemCursor.
    std::array<SDL_Cursor*, SDL_SYSTEM_CURSOR_COUNT> Cursors{};
};
