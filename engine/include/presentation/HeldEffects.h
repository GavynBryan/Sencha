#pragma once

#include <camera/CameraEffectTiming.h>
#include <ecs/World.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// For presented effects that each keep their CameraEffectTiming as `Timing`.

template <typename Effect>
void ReleaseHeldEffects(std::vector<Effect>& effects, const LeaseOwner& holder, std::uint64_t tick,
                        double tickSeconds)
{
    for (Effect& effect : effects)
        if (effect.Timing.Holder.IsSet() && effect.Timing.Holder == holder)
            effect.Timing.ReleaseAfter(tick, tickSeconds);
}

// Past `capacity` the weakest by `strength` gives way to the new one.
template <typename Effect, typename Strength>
void StartHeldEffect(std::vector<Effect>& effects, std::size_t capacity, Effect effect, Strength strength)
{
    if (effects.size() < capacity)
    {
        effects.push_back(std::move(effect));
        return;
    }
    *std::ranges::min_element(effects, {}, strength) = std::move(effect);
}

// Lets go of what a holder that has died was holding, then drops what has run out.
template <typename Effect>
void RetireHeldEffects(std::vector<Effect>& effects, const World& world, const CameraEffectClock& now)
{
    std::erase_if(effects, [&](Effect& effect) {
        if (effect.Timing.Holder.IsSet() && !world.IsAlive(effect.Timing.Holder.Entity))
            effect.Timing.ReleaseAfter(now.Tick == 0 ? 0 : now.Tick - 1, now.TickSeconds);
        return effect.Timing.Expired(effect.Timing.Elapsed(now));
    });
}
