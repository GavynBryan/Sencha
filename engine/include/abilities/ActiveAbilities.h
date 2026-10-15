#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityPresentationState.h>
#include <abilities/AbilityGrantId.h>
#include <abilities/AbilityId.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <input/InputActionSource.h>
#include <math/Vec.h>

#include <authored/VerbId.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

enum class AbilityEndReason : std::uint8_t
{
    None,
    Completed,
    InputReleased,
    InputInterrupted,
    InputLost,
    GrantRevoked,
    ContinuationFailed,
    CancelTriggered,
    CancelledByAbility,
    TargetLost,
    CommitFailed,
    LeaseRefused,
    ActorEnded,
    ZoneDetached,
    AuthorityDenied,
    HotReload,
    ExplicitCancel,
    StepBudget,
    CapacityExceeded,
};

[[nodiscard]] std::string_view AbilityEndReasonName(AbilityEndReason reason);

// Which start this is: the command tick, the ability, and where the start came from
// -- the index of the press in that tick's edge list, or kAbilityRequestOrigin plus
// the request's place in the queue. Tied to the attempt rather than to a count of
// admitted starts, so two machines that admit differently still name the same
// press the same way.
struct AbilityActivationKey
{
    std::uint64_t CommandTick = 0;
    AbilityId Ability;
    std::uint8_t Origin = 0;

    friend bool operator==(const AbilityActivationKey&, const AbilityActivationKey&) = default;
};

inline constexpr std::uint8_t kAbilityRequestOrigin = 0x80;

// The activation as every machine names it: its key with the ability by name and
// the grant it ran through, so a client's guess and the authority's record of one
// press are one activation, and two grants observing that press are two.
[[nodiscard]] std::uint64_t AbilityActivationIdentity(const AbilityActivationKey& key, std::uint32_t abilityWireKey,
                                                      AbilityGrantId grant);
inline constexpr std::uint8_t kAbilityNoTrigger = 0xFF;

enum class AbilityRecordState : std::uint8_t
{
    Free,
    Live,
    Lingering,
};

struct AbilityHeldLease
{
    VerbId Lease;
    std::uint8_t Op = 0;
};

struct AbilityRecord
{
    AbilityActivationKey Key;
    AbilityGrantId Grant;
    AbilityRecordState State = AbilityRecordState::Free;
    // The definition revision it started under; a reload moves it.
    std::uint32_t Revision = 0;
    std::uint8_t Phase = 0;
    // The action and source a press started it from, when one did.
    std::uint8_t TriggerAction = kAbilityNoTrigger;
    InputActionSourceId TriggerSource = kLocalInputActionSource;
    // The trigger was released (or, for a toggle, pressed again) since the start.
    bool Released = false;
    // Phase entry, on the phase's own clock: actor ticks or simulation ticks.
    std::uint64_t PhaseStart = 0;
    std::uint32_t Pulse = 0;
    std::uint32_t NextPulse = 0;
    // A bit per once-group already paid.
    std::uint8_t PaidGroups = 0;
    bool CooldownStarted = false;
    std::uint8_t LeaseCount = 0;
    AbilityHeldLease Leases[kAbilityRecordLeaseCapacity] = {};
    std::uint8_t OwnedTagCount = 0;
    GameplayTagId OwnedTags[kAbilityOwnedTagCapacity] = {};
    EntityId Instigator;
    AbilityEndReason EndReason = AbilityEndReason::None;
    // The condition or element whose failure ended it, by index; 0xFFFF when none.
    std::uint16_t EndElement = 0xFFFF;
    std::uint64_t EndTick = 0;
    // The last hit its pulse cues struck, for presenting the latest pulse.
    Vec3d LastPoint = Vec3d::Zero();
    Vec3d LastNormal = Vec3d::Zero();
    EntityId LastTarget;
    // Start order on this machine, which lingering eviction keys on.
    std::uint64_t Sequence = 0;
    // AbilityActivationIdentity: what its leases, presentation and cues are keyed by.
    std::uint64_t Identity = 0;
    // This machine's guess at its own pawn's activation, run here ahead of the
    // authority; never sent. A record the wire delivered is only presented here.
    bool Predicted = false;
};

// What an actor is running, and what it ran recently enough to still present.
struct SENCHA_COMPONENT("sencha.active_abilities") ActiveAbilities
{
    // The authority's records, then this machine's guesses (AbilityCapacity.h).
    AbilityRecord Records[kAbilitySlotCapacity] = {};
    std::uint64_t NextSequence = 1;
    // Lingering records evicted to make room for a live one.
    std::uint32_t Evicted = 0;
};

// A record the authority sent while this machine runs its own guess at the same
// activation: kept as the authority's word, but neither counted, run nor presented.
[[nodiscard]] bool IsAbilityRecordShadowed(const ActiveAbilities& active, std::size_t slot);

static_assert(std::is_trivially_copyable_v<ActiveAbilities>,
              "ActiveAbilities must be trivially copyable to live in ECS chunks");

// Wherever records are, they are presented: on a machine that only receives them,
// the actor's cue log and what this machine has shown of both come with them.
template<>
struct ComponentTraits<ActiveAbilities>
{
    using DerivedComponents = std::tuple<AbilityCueLog, AbilityPredictedCues, AbilityPresentationState>;
};

#if !defined(SENCHA_CODEGEN)
#  include <abilities/ActiveAbilities.sencha.h>
#  include <abilities/ActiveAbilitiesWire.h>
#endif
