#pragma once

#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputFrame.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/FrameClock.h>
#include <world/RuntimeWorld.h>

#include <SDL3/SDL.h>

#include <span>

//=============================================================================
// Game and frame contexts
//
// Each hook and frame phase receives the per-call data its consumer may touch.
// Runtime simulation contexts expose one entity World and one domain-specific
// storage-partition set. No context exposes registries, registry IDs, or another
// entity universe.
//=============================================================================

class Engine;
class EngineSchedule;

struct GameConfigureContext
{
    EngineConfig& Config;
};

struct GameStartupContext
{
    EngineConfig& Config;
};

struct GameShutdownContext
{
    EngineConfig& Config;
};

struct SystemRegisterContext
{
    EngineConfig& Config;
    EngineSchedule& Schedule;
};

struct PlatformEventContext
{
    EngineConfig& Config;
    SDL_Event& Event;
    bool Handled = false;
};

// Retained backend owners consume the stable zone-lifecycle batch before a new
// frame view is built. The World remains live, including importing/detaching
// partitions named by the records.
struct ZoneResidencyContext
{
    EngineConfig& Config;
    World& Entities;
    std::span<const ZoneResidencyChange> Changes;
};

// Shared shape for runtime phases. Partitions is always the correct domain set
// for that phase: Logic, Physics, Visible, Audio, or Resident as documented.
struct FixedLogicContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct PhysicsContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct PostFixedContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct FrameUpdateContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    double WallDeltaSeconds = 0.0;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct RenderExtractContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    RenderPacket& PacketWrite;
    RenderPacket& PacketRead;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct AudioContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct EndFrameContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
    bool LifecycleOnly = false;
};
