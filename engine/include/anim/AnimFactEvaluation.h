#pragma once

#include <anim/AnimFacts.h>
#include <anim/AnimRigBinding.h>

#include <cstdint>
#include <span>

// Resets `history` when it was kept for another binding generation or never started.
void BeginAnimFactObservation(const AnimBoundRig& rig, AnimFactHistory& history, AnimTick now);

void EvaluateAnimDerivations(const AnimBoundRig& rig,
                             std::span<std::uint32_t> values,
                             AnimFactHistory& history,
                             AnimTick now,
                             double tickSeconds);

[[nodiscard]] bool AreAnimDerivedFactsExact(const AnimBoundRig& rig,
                                            const AnimFactHistory& history,
                                            AnimTick now,
                                            double tickSeconds);
