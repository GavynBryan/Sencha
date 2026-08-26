#pragma once

#include <anim/AnimationClipCache.h>

class World;
class StoragePartitionSet;
class EngineSchedule;
struct FixedLogicContext;

//=============================================================================
// AnimationClipPlaybackSystem
//
// Advances every clip player's time by the fixed tick, wrapping looped
// clips at their duration and holding non-looped ones at their last frame.
// Time is the only state it touches: sampling happens at render extract,
// which keeps pose evaluation once per rendered frame rather than once per
// tick, and keeps this deterministic under 0..N-tick catch-up frames.
//=============================================================================
class AnimationClipPlaybackSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};

// Registers clip playback on a game's schedule, the RegisterAbilityKitSystems
// shape: an engine-owned system a host opts into rather than one the engine
// forces on every schedule.
void RegisterAnimationSystems(EngineSchedule& schedule);

// The pure half: advances players in `world` by `deltaSeconds`. Clip
// durations come from `clips`; a player whose clip is not resident holds
// its time (a streaming gap must not silently rewind an animation).
void AdvanceAnimationClipPlayers(World& world,
                                 const StoragePartitionSet& partitions,
                                 const AnimationClipCache& clips,
                                 float deltaSeconds);
