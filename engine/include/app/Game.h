#pragma once

#include <app/GameContexts.h>

//=============================================================================
// Game
//
// Base interface for application-specific configuration and engine callbacks.
// Games override lifecycle hooks to register systems and react to platform events.
//=============================================================================
class Game
{
public:
    virtual ~Game() = default;

    virtual void OnConfigure(GameConfigureContext&) {}
    virtual void OnStart(GameStartupContext&) {}
    virtual void OnRegisterSystems(SystemRegisterContext&) {}
    virtual void OnPlatformEvent(PlatformEventContext&) {}
    virtual void OnShutdown(GameShutdownContext&) {}
};
