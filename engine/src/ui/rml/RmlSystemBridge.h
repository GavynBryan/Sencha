#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <SDL3/SDL_mouse.h>

#include <array>
#include <chrono>

class UiDiagnosticLog;

// The document engine's ambient services answered with Sencha's: a presentation
// wall clock that runs while the simulation is paused, diagnostics as data, the
// clipboard and the pointer's shape (docs/ui/architecture.md §13, Diagnostics).
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
