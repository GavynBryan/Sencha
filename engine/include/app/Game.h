#pragma once

#include <app/GameContexts.h>

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
