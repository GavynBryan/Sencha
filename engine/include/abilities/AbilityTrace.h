#pragma once

#include <abilities/ActiveAbilities.h>
#include <authored/VerbInvocation.h>
#include <core/containers/BoundedRing.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <cstdint>
#include <string_view>

enum class AbilityRefusal : std::uint8_t
{
    None,
    NotGranted,
    Undefined,
    ActivationFailed,
    Instances,
    Capacity,
    CostFailed,
};

[[nodiscard]] std::string_view AbilityRefusalName(AbilityRefusal refusal);

enum class AbilityTraceEvent : std::uint8_t
{
    // Admission said no; Refusal says why, Element names the failing condition.
    Refused,
    Started,
    // EndReason says why; Element names the condition or op behind it, if one.
    Ended,
    // Element is the transition taken, Phase the phase entered.
    Transition,
    // An op's guard was false, or its entity was gone (Admission == UnresolvedReference).
    Skipped,
    // An invoke, lease or query ran; Admission is what the operation answered.
    Operated,
    // A cue op fired.
    Cue,
    // Equal-priority exclusive bindings on one press: nothing started.
    BindingAmbiguous,
    // An exclusive binding was refused and consumed its press.
    Blocked,
    // A commit paid nothing, because a once group was already paid.
    AlreadyCommitted,
};

// Compact ids only: the definitions they index can reload, and an inspector resolves
// keys and names against whatever is current when it reads.
struct AbilityTraceRecord
{
    std::uint64_t Tick = 0;
    EntityId Actor{};
    AbilityActivationKey Key{};
    AbilityTraceEvent Event = AbilityTraceEvent::Started;
    AbilityRefusal Refusal = AbilityRefusal::None;
    AbilityEndReason EndReason = AbilityEndReason::None;
    VerbAdmission Admission = VerbAdmission::Accepted;
    std::uint8_t Phase = 0;
    std::uint16_t Element = 0xFFFF;
    GameplayTagId Cue{};
    Vec3d At = Vec3d::Zero();
};

// The World's ability history, for answering why an ability did or did not do
// something after the fact.
class AbilityTrace
{
public:
    explicit AbilityTrace(std::size_t capacity = 512)
        : Ring(capacity)
    {
    }

    void Record(const AbilityTraceRecord& record) { Ring.Push(record); }
    [[nodiscard]] const BoundedRing<AbilityTraceRecord>& Records() const { return Ring; }
    void Clear() { Ring.Clear(); }

private:
    BoundedRing<AbilityTraceRecord> Ring;
};
