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

//=============================================================================
// FrameUpdateContext
//
// Per-frame game update. Runs once per rendered frame between Simulate and
// ExtractRenderPacket. Use for state that must update at render cadence:
// camera orientation from mouse look, HUD animations, menu timers, debug
// cameras, anything input-reactive that is not simulation state.
//
// WallDeltaSeconds is the raw wall-clock duration of the previous frame.
// Use it for framerate-independent interpolation (e.g. exponential smoothing).
// Do NOT use it to scale simulation-authoritative values — that belongs in
// OnFixedUpdate with FixedSimTime.DeltaSeconds.
//=============================================================================
struct FrameUpdateContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    double WallDeltaSeconds = 0.0;
    PresentationTime Presentation;
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
