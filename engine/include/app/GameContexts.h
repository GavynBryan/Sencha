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
//
// Every record names the partition it concerns, so a subscriber can act on a
// departing zone's entities from Changes alone, and can move one out to
// PersistentStoragePartition to let it outlive the zone. Resolving an arbitrary
// ZoneId to its partition is deliberately absent: a subscriber that migrates
// between named zones is a system with its own RuntimeWorld reference, the way
// PhysicsStepSystem owns its PhysicsWorld, and widening this context would make
// every focused residency test boot the zone runtime to get it.
struct ZoneResidencyContext
{
    EngineConfig& Config;
    World& Entities;
    std::span<const ZoneResidencyChange> Changes;
};

// Runs once per rendered frame, after the frame view is built and before any
// fixed tick, including on frames that run no ticks at all.
//
// This is where frame-rate state that simulation reads is produced. Look
// accumulation is the motivating case: a camera turn arrives with the frame, but
// the character steers on it during the tick, so accumulating it in FrameUpdate
// left every tick steering on the previous frame's orientation while the same
// frame rendered the new one.
//
// There is deliberately no PresentationTime here: presentation is built after
// simulation, so no valid alpha exists yet. Work that needs one belongs in
// FrameUpdate.
//
// This is the one simulation-side context carrying the raw InputFrame, because
// it is where the input mapper reads devices and turns them into actions.
// Gameplay reads InputActionState instead.
struct PreSimulateContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    InputFrame& Input;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

// Shared shape for runtime phases. Partitions is always the correct domain set
// for that phase: Logic, Physics, Visible, Audio, or Resident as documented.
//
// No InputFrame: simulation reads resolved actions from InputActionState, which
// the mapper filled during PreSimulate. A fixed tick that read devices directly
// would have no defined answer on a frame that ran several ticks.
struct FixedLogicContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;

    // Ticks this frame has still to run, this one included: 1 on a single-tick
    // frame, N down to 1 across a catch-up burst. A system distributing a
    // per-frame quantity across the burst -- a displacement latched once per
    // frame, spent by however many ticks that frame runs -- divides by it.
    // Giving the whole frame's displacement to the first tick and nothing to
    // the rest is what makes a heading advance in steps that do not match the
    // simulated time they cover.
    uint32_t TicksLeftInFrame = 1;
};

struct PhysicsContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct PostFixedContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    FixedSimTime Time;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

// Keeps the raw InputFrame: editor viewports, debug tooling, and a future
// rebinding screen all need to know which physical control moved, and all of
// them live on the presentation clock.
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
    // Audio runs once per rendered frame, so anything here that ages with the
    // player's experience rather than with simulation -- subtitle dwell time,
    // for instance -- uses this rather than the tick delta.
    double WallDeltaSeconds = 0.0;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
};

struct EndFrameContext
{
    EngineConfig& Config;
    RuntimeFrameLoop& Runtime;
    PresentationTime Presentation;
    World& Entities;
    const StoragePartitionSet& Partitions;
    bool LifecycleOnly = false;
};
