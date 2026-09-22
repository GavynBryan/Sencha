#pragma once

#include <anim/AnimFacts.h>
#include <anim/AnimRigBinding.h>

#include <cstdint>
#include <span>

//=============================================================================
// Fact derivation
//
// Runs a bound rig's derivations over one entity's gathered slots, in
// declaration order, so every derivation reads values already final for the
// tick. Pure over its arguments: the gather system and the preview both call
// it, and a test drives it tick by tick without a World.
//
// Memory is per derivation (AnimFactHistory). The first tick an entity is
// observed carries no prior value, so an edge cannot be seen on it and a
// duration starts counting from it; once one horizon has passed every temporal
// result is exact. Smoothing converges rather than becoming exact, at the rate
// its time constant sets.
//=============================================================================

// Resets `history` for `rig` when it was kept for a different binding
// generation or never started, marking `now` as the first observed tick.
void BeginAnimFactObservation(const AnimBoundRig& rig, AnimFactHistory& history, AnimTick now);

void EvaluateAnimDerivations(const AnimBoundRig& rig,
                             std::span<std::uint32_t> values,
                             AnimFactHistory& history,
                             AnimTick now,
                             double tickSeconds);

// Whether every derived fact is exact at `now`: the entity has been observed
// for at least the rig's horizon.
[[nodiscard]] bool AreAnimDerivedFactsExact(const AnimBoundRig& rig,
                                            const AnimFactHistory& history,
                                            AnimTick now,
                                            double tickSeconds);
