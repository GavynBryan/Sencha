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

// Registers the resolve system. Order any system that reads actions after it
// with EngineSchedule::After.
void RegisterInputSystems(EngineSchedule& schedule,
                          DataAssetCache& dataAssets,
                          LoggingProvider& logging);
