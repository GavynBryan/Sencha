#pragma once

#include <core/config/EngineConfig.h>
#include <input/InputFrame.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/FrameClock.h>
#include <world/registry/FrameRegistryView.h>

#include <SDL3/SDL.h>

#include <span>

//=============================================================================
// Game and frame contexts
//
// Each hook and frame phase receives a context carrying the per-call data its
// consumer needs -- config, input, timing, registry views, render packets.
// Contexts do NOT carry an engine handle. A game reaches the engine it drives
// through Game::GetEngine() (bound once by Engine::Run); a system takes the
// engine dependencies it needs as constructor parameters at registration. So a
// context stays an honest statement of what its consumer may touch.
//=============================================================================

class Engine;
class EngineSchedule;

//=============================================================================
// GameConfigureContext
//
// Provides mutable engine configuration before the Engine is constructed.
// Used by a Game to choose startup settings and service options.
//=============================================================================
struct GameConfigureContext
{
    EngineConfig& Config;
};

//=============================================================================
// GameStartupContext
//
// Per-call configuration for game startup; the engine is reached via
// Game::GetEngine(). Runs after engine initialization, before frame processing.
//=============================================================================
struct GameStartupContext
{
    EngineConfig& Config;
};

//=============================================================================
// GameShutdownContext
//
// Per-call configuration for game shutdown; the engine is reached via
// Game::GetEngine(). Used for game-owned cleanup while services are reachable.
//=============================================================================
struct GameShutdownContext
{
    EngineConfig& Config;
};

//=============================================================================
// SystemRegisterContext
//
// Provides access to the engine schedule during system registration.
// Used by a Game to add systems and declare their execution dependencies.
//=============================================================================
struct SystemRegisterContext
{
    EngineConfig& Config;
    EngineSchedule& Schedule;
};

//=============================================================================
// PlatformEventContext
//
// Wraps an SDL platform event with a handled flag.
// Used by a Game to consume window, input, and platform messages.
//=============================================================================
struct PlatformEventContext
{
    EngineConfig& Config;
    SDL_Event& Event;
    bool Handled = false;
};

//=============================================================================
// FixedLogicContext
//
// Provides fixed-tick simulation state before physics runs.
// Used for deterministic gameplay logic that advances on fixed time steps.
//=============================================================================
struct FixedLogicContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// PhysicsContext
//
// Provides fixed-tick simulation state for physics systems.
// Used for collision, dynamics, and other physics-authoritative updates.
//=============================================================================
struct PhysicsContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// PostFixedContext
//
// Provides fixed-tick simulation state after physics has completed and the
// engine has restored world-transform coherence for logic registries.
//=============================================================================
struct PostFixedContext
{
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
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    double WallDeltaSeconds = 0.0;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// RenderExtractContext
//
// Provides presentation-time state and double-buffered render packets.
// Used to extract renderable data from simulation state for the renderer.
// World transforms are coherent for visible registries at phase entry.
//=============================================================================
struct RenderExtractContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    RenderPacket& PacketWrite;
    RenderPacket& PacketRead;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// AudioContext
//
// Provides presentation-time state for audio systems.
// Used to update audio playback from current input and world registry views.
//=============================================================================
struct AudioContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
};

//=============================================================================
// EndFrameContext
//
// Provides final per-frame state before the engine advances frame resources.
// Used for cleanup, lifecycle-only work, and end-of-frame bookkeeping.
//=============================================================================
struct EndFrameContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    FrameRegistryView Registries;
    std::span<Registry*> ActiveRegistries;
    bool LifecycleOnly = false;
};
