#pragma once

#include <app/Game.h>

#include <memory>

class EditorServices;

// The editor's Game entry point: it owns the EditorServices composition root and
// forwards each Game lifecycle hook to it. All subsystem ownership and wiring
// lives in EditorServices; this stays glue.
class EditorApp : public Game
{
public:
    // Defined out of line so the .cpp sees the complete EditorServices the
    // unique_ptr member forward-declares.
    EditorApp();
    ~EditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::unique_ptr<EditorServices> Services;
};
