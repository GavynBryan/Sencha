#pragma once

#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityPresentationState.h>
#include <abilities/ActiveAbilities.h>
#include <app/GameContexts.h>
#include <ecs/Query.h>

#include <cstdint>
#include <optional>
#include <vector>

class VerbDispatcher;
class World;

// How old a discrete cue may be when an actor is first seen and still be presented.
inline constexpr std::uint64_t kAbilityCueWindowTicks = 30;

// At most this many pulses are presented as one occurrence a tick, however many ran.
inline constexpr std::uint32_t kAbilityPulsesPerOccurrence = 8;

//=============================================================================
// AbilityPresentationSystem
// Where presentation runs, turns what the abilities did into their cues'
// presentations (CuePresentationLibrary): each discrete cue in an actor's log
// once, each record's new pulses as one occurrence, and each live phase's
// present entries as leases held while the phase lasts. Reads only replicated
// state, so every presenting machine shows the same occurrences
// (docs/gameplay/abilitykit.md, "Presentation").
//=============================================================================
class AbilityPresentationSystem
{
public:
    explicit AbilityPresentationSystem(VerbDispatcher* verbs)
        : Verbs(verbs)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);
    // One tick's presentation outside a schedule, for tests and previews.
    void Present(World& world, std::uint64_t tick);

private:
    VerbDispatcher* Verbs = nullptr;
    const World* LastWorld = nullptr;
    std::optional<Query<Read<ActiveAbilities>, Read<AbilityCueLog>, Read<AbilityPresentationState>>> Actors;
    std::vector<EntityId> Visited;
};
