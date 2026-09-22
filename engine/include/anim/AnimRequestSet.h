#pragma once

#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

//=============================================================================
// AnimRequestSet
//
// Gameplay's intent toward animation, as eight fixed records: the only
// animation-facing data gameplay writes and the only animation-facing data
// that replicates. A request says "present this intent, from this tick, on
// these layers, until I say otherwise". It carries no priority, no target, no
// handler and no reply: precedence lives in selector rules, and anything that
// looks like a message to gameplay is a verb travelling the wrong direction.
//
// Mutated through the request API (AnimRequests.h), never field by field:
// supersession, deduplication, capacity and terminal retention are rules of the
// set, not of each caller.
//=============================================================================

// Which request, for cancellation and for the decision log. The source is the
// participant or entity whose action created it -- provenance, and the
// Instigator of any request-driven animation event, never a target. The
// sequence is the set's own, replicated with the record, so ordering by it
// picks the same record on every machine.
struct AnimRequestId
{
    EntityId Source;
    std::uint32_t Sequence = 0;

    [[nodiscard]] bool IsValid() const { return Sequence != 0; }
    friend bool operator==(const AnimRequestId&, const AnimRequestId&) = default;
};

inline constexpr std::uint8_t kAnimAllLayers = 0xFF;
inline constexpr std::uint8_t kAnimNoAnchorSection = 0xFF;

struct AnimRequest
{
    AnimRequestId Id;
    GameplayTagId Intent;
    // The originating ability. Debugging only; never read by a rule.
    GameplayTagId SourceTag;

    // The server tick the intent began. Content time is now - StartTick.
    AnimTick StartTick = 0;
    // Fixed: how long it lives. Held and Impulse ignore it.
    std::uint32_t FixedTicks = 0;
    std::uint32_t Params[kAnimRequestParams] = {};

    // Set when the request is cancelled, and kept while its tail is still
    // reconstructible: a late joiner has to know a request was cancelled for
    // as long as a latch or cancel section is playing it out. The reason is
    // what ReqCancelReason reports on the cancel tick.
    AnimTick CancelTick = 0;
    AnimTick TailUntilTick = 0;

    // Flow progress, written only by the simulation authority when its flow
    // runner crosses a section boundary or enters the cancel section.
    AnimTick AnchorSectionStartTick = 0;

    std::uint8_t Layers = kAnimAllLayers;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    AnimCancelReason CancelReason = AnimCancelReason::None;
    std::uint8_t AnchorSection = kAnimNoAnchorSection;
    bool Occupied = false;

    [[nodiscard]] bool IsCancelled() const { return CancelReason != AnimCancelReason::None; }
};

struct SENCHA_COMPONENT("sencha.anim_request_set") AnimRequestSet
{
    AnimRequest Records[kAnimRequestCapacity] = {};
    std::uint32_t NextSequence = 1;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRequestSet.sencha.h>
#endif
