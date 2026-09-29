#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <authored/VerbInvocation.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>
#include <string_view>

enum class AnimDecisionCause : std::uint8_t
{
    RequestAdded,
    RequestSuperseded,
    RequestDeduplicated,
    RequestCancelled,
    RequestExpired,
    RequestRejected,
    WinnerChanged,
    LatchArmed,
    LatchReleased,
    LatchInterrupted,
    ContentChanged,
    // A rebind could not keep an index a layer held and reset it.
    IndexReset,
    SectionChanged,
    // EventOutcome and Admission say what came of it.
    EventCrossed,
    BehaviorEntered,
    BehaviorExited,
    SectionEntered,
    SectionExited,
    // Rule indexes the selector's weight rules, or is none for the rig's constant.
    WeightChanged,
    BlendApplied,
    // This machine's AnimRigTimingIdentity started or stopped matching the one the
    // authority's requests were made under; reconstruction is untrustworthy meanwhile.
    TimingDisagreed,
    TimingAgreed,
    // A Held request outlived its source or owner: the producer that holds it never
    // cancelled it. Reported once per request; animation does not end it.
    RequestOrphaned,
};

enum class AnimEventOutcome : std::uint8_t
{
    // Offered to its binding; Admission is the answer.
    Fired,
    // Passed over by a skip in time rather than played through.
    Skipped,
    BelowWeight,
};

[[nodiscard]] std::string_view AnimEventOutcomeName(AnimEventOutcome outcome);

enum class AnimChangeReason : std::uint8_t
{
    None,
    FactsChanged,
    RequestsChanged,
    HoldExpired,
    LatchComplete,
    LatchInterrupted,
    Rebound,
    BehaviorChanged,
    RowChanged,
    FlowStarted,
    SectionFollowed,
    SectionLooped,
    SectionBranched,
    SectionCancelled,
    FlowAnchored,
    // The request driving the content changed but its behavior did not.
    RequestSuperseded,
    // The authority moved the start of the request a layer was playing.
    RequestCorrected,
    // Rebuilt from the authority's requests after a refused prediction.
    Reconstructed,
};

[[nodiscard]] std::string_view AnimChangeReasonName(AnimChangeReason reason);

[[nodiscard]] std::string_view AnimDecisionCauseName(AnimDecisionCause cause);

// Capacity rejects rather than evicts, so server and client decide identically.
enum class AnimRejectReason : std::uint8_t
{
    None,
    Capacity,
    UndeclaredIntent,
    // The request names no source, no intent, or no layer.
    Malformed,
    // A client asked for another entity's request, which only the authority issues.
    LeftToAuthority,
};

[[nodiscard]] std::string_view AnimRejectReasonName(AnimRejectReason reason);

inline constexpr std::uint8_t kAnimNoLayer = 0xFF;

struct AnimDecisionRecord
{
    AnimTick Tick = 0;
    AnimDecisionCause Cause = AnimDecisionCause::RequestAdded;
    std::uint8_t Layer = kAnimNoLayer;
    AnimRequestId Request;
    GameplayTagId Intent;
    AnimCancelReason CancelReason = AnimCancelReason::None;
    AnimRejectReason RejectReason = AnimRejectReason::None;
    AnimChangeReason Reason = AnimChangeReason::None;
    // Winner and latch records: the flattened rules before and after.
    std::uint16_t Rule = kAnimNoRule;
    std::uint16_t PreviousRule = kAnimNoRule;
    GameplayTagId Behavior;
    // Content records: the row and content resolved.
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // Event records, including lifecycle events.
    std::uint32_t EventKey = 0;
    AnimEventOutcome EventOutcome = AnimEventOutcome::Fired;
    VerbAdmission Admission = VerbAdmission::Accepted;
    // Section records, including section lifecycle events.
    std::uint8_t Section = 0xFF;
    std::uint8_t PreviousSection = 0xFF;
    // Blend records. BlendMagnitude is the largest joint offset an inertialization
    // began from.
    GameplayTagId PreviousBehavior;
    float BlendSeconds = 0.0f;
    float BlendMagnitude = 0.0f;
    AnimBlendMode Blend = AnimBlendMode::Snap;
    bool BlendOverridden = false;
};

inline constexpr std::size_t kAnimDecisionLogCapacity = 64;

struct SENCHA_COMPONENT("sencha.anim_decision_log") AnimDecisionLog
{
    AnimDecisionRecord Records[kAnimDecisionLogCapacity] = {};
    // Records ever written; the ring holds the newest min(Written, capacity).
    std::uint64_t Written = 0;

    void Append(const AnimDecisionRecord& record)
    {
        Records[Written % kAnimDecisionLogCapacity] = record;
        ++Written;
    }

    [[nodiscard]] std::size_t Size() const
    {
        return Written < kAnimDecisionLogCapacity ? static_cast<std::size_t>(Written)
                                                  : kAnimDecisionLogCapacity;
    }

    // Oldest first: index 0 is the oldest record still held.
    [[nodiscard]] const AnimDecisionRecord& At(std::size_t index) const
    {
        const std::uint64_t first = Written - Size();
        return Records[(first + index) % kAnimDecisionLogCapacity];
    }
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimDecisionLog.sencha.h>
#endif
