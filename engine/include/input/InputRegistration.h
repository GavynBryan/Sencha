#pragma once

#include <input/InputBindingCache.h>

class DataAssetCache;
class EngineSchedule;
class LoggingProvider;
class World;

//=============================================================================
// Input mapping registration
//
// The world resources a game touches:
//
//   InputProfileBinding -- which profile is live. Replacing the handle swaps
//                          the whole control scheme; contexts that survive the
//                          swap by name stay active.
//   InputActionState    -- resolved actions, read by gameplay.
//   InputContextSet     -- runtime activation, by lease.
//   InputBindingCache   -- compiled tables, rebound on hot reload.
//=============================================================================

struct InputProfileBinding
{
    InputProfileHandle Profile;
};

// Creates the input resources and points them at a profile. Safe to call again
// with a different profile to replace the control scheme at runtime.
void RegisterInputMapping(World& world, DataAssetCache& dataAssets, InputProfileHandle profile);

// Registers the resolve system, which resolves in two phases: PreSimulate for
// the presentation snapshot, FixedLogic for each tick's record.
//
// Ordering a reader after it depends on which clock the reader is on, because
// EngineSchedule::After is phase-local -- an edge between systems that share no
// phase list records nothing and asserts.
//
//   FixedLogic or PreSimulate readers (InputActionState::Tick()) share a phase
//   with the resolve system, so they need the edge:
//       schedule.After<CharacterInputSystem, InputActionResolveSystem>();
//
//   FrameUpdate readers (InputActionState::Frame(), the presentation snapshot a
//   camera or menu wants) must NOT declare one. PreSimulate runs in
//   FramePhase::ScheduleTicks and FrameUpdate in FramePhase::Update, so the
//   frame order already puts resolution first; the edge would only assert.
void RegisterInputSystems(EngineSchedule& schedule,
                          DataAssetCache& dataAssets,
                          LoggingProvider& logging);
