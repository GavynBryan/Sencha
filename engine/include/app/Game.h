#pragma once

#include <app/GameContexts.h>

class Game
{
public:
    virtual ~Game() = default;

    virtual void OnConfigure(GameConfigureContext&) {}
    virtual void OnStart(GameStartupContext&) {}
    virtual void OnPlatformEvent(PlatformEventContext&) {}
    virtual void OnFixedUpdate(FixedUpdateContext&) {}
    virtual void OnUpdate(FrameUpdateContext&) {}
    virtual void OnExtractRender(RenderExtractContext&) {}
    virtual void OnShutdown(GameShutdownContext&) {}
};
