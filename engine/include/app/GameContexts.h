#pragma once

#include <core/config/EngineConfig.h>
#include <input/InputFrame.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/FrameClock.h>

#include <SDL3/SDL.h>

class Engine;

struct GameConfigureContext
{
    EngineConfig& Config;
};

struct GameStartupContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
};

struct GameShutdownContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
};

struct PlatformEventContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    SDL_Event& Event;
    bool Handled = false;
};

struct FixedUpdateContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
};

struct RenderExtractContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    RenderPacket& PacketWrite;
    RenderPacket& PacketRead;
    PresentationTime Presentation;
    bool AllowDefaultRenderScene = true;
};
