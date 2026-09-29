#pragma once

#include <anim/AnimTypes.h>
#include <authored/VerbId.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

// The source is provenance and the instigator of request-driven events, never a
// target. The sequence replicates with the record, so ordering by it agrees everywhere.
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

    // Kept while a latch or cancel section plays the cancelled request out, so a late
    // joiner knows it was cancelled.
    AnimTick CancelTick = 0;
    AnimTick TailUntilTick = 0;

    // Written only by the authority's flow runner at a section boundary or cancel.
    AnimTick AnchorSectionStartTick = 0;
    // The command whose processing issued it, on the command timeline: what a
    // prediction and the authority's record are matched by.
    AnimTick Command = 0;

    // Local only, never replicated: the entity whose lifetime bounds a Held request,
    // and the invocation that asked for it.
    EntityId Owner;
    InvocationId Cause;

    std::uint8_t Layers = kAnimAllLayers;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    AnimCancelReason CancelReason = AnimCancelReason::None;
    std::uint8_t AnchorSection = kAnimNoAnchorSection;
    // Bit i set when Params[i] is a gameplay tag id. Tag ids are registration order,
    // so these are renamed on their way to another machine.
    std::uint8_t TagParams = 0;
    bool Occupied = false;
    // Issued here ahead of the authority (AnimRequestJournal). Never replicated, so
    // the authority's set arriving clears it.
    bool Predicted = false;

    [[nodiscard]] bool IsCancelled() const { return CancelReason != AnimCancelReason::None; }
};

struct SENCHA_COMPONENT("sencha.anim_request_set") AnimRequestSet
{
    AnimRequest Records[kAnimRequestCapacity] = {};
    std::uint32_t NextSequence = 1;
    // The authority's AnimRigTimingIdentity these records were made under; zero until stamped.
    std::uint64_t RigTiming = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRequestSet.sencha.h>
#  include <anim/AnimRequestWire.h>
#endif
