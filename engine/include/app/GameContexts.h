#pragma once

#include <core/config/EngineConfig.h>
#include <input/InputFrame.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/FrameClock.h>
#include <world/registry/FrameRegistryView.h>

#include <SDL3/SDL.h>

#include <span>

class Engine;
class EngineSchedule;

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

struct SystemRegisterContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    EngineSchedule& Schedule;
};

struct PlatformEventContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    SDL_Event& Event;
    bool Handled = false;
};

struct FixedLogicContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

struct PhysicsContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

struct PostFixedContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// FrameUpdateContext
//
// Per-frame systems. Runs once per rendered frame between Simulate and
// ExtractRenderPacket. Use for state that must update at render cadence:
// camera orientation from mouse look, HUD animations, menu timers, debug
// cameras, anything input-reactive that is not simulation state.
//
// WallDeltaSeconds is the raw wall-clock duration of the previous frame.
// Use it for framerate-independent interpolation (e.g. exponential smoothing).
// Do NOT use it to scale simulation-authoritative values; that belongs in
// fixed-tick systems with FixedSimTime.DeltaSeconds.
//=============================================================================
struct FrameUpdateContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    double WallDeltaSeconds = 0.0;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
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
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

struct AudioContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

struct EndFrameContext
{
    Engine& EngineInstance;
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
    bool LifecycleOnly = false;
};
