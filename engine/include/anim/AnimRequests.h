#pragma once

#include <anim/AnimDecisionLog.h>
#include <anim/AnimRequestSet.h>

#include <array>
#include <cstdint>

class World;

//=============================================================================
// The request lifecycle
//
// Issue and cancel are the only ways a request set changes, and their rules are
// the set's, so every caller -- an ability, a preview scenario, a replicated
// snapshot applying -- gets the same outcome for the same inputs:
//
//   - expired Fixed and Impulse records, and cancelled records whose tail has
//     ended, are pruned before any insert;
//   - a Held or Fixed request from the same source with the same intent as a
//     live record supersedes it in place, which is how a combo advances;
//   - an Impulse is deduplicated per (source, intent, tick) and never
//     supersedes anything;
//   - with every record occupied the new request is rejected. Nothing is
//     evicted, so server and client decide identically.
//
// Pure over the component data. The World overloads find the components,
// validate the intent against the entity's rig, and write the decision log.
//=============================================================================

struct AnimRequestDesc
{
    EntityId Source;
    GameplayTagId Intent;
    GameplayTagId SourceTag;
    std::uint8_t Layers = kAnimAllLayers;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 0;
    std::array<std::uint32_t, kAnimRequestParams> Params{};
    // Which params are gameplay tag ids; see AnimRequest::TagParams. The World
    // form sets it from the rig's request schema.
    std::uint8_t TagParams = 0;
};

enum class AnimRequestStatus : std::uint8_t
{
    Accepted,
    // Accepted in place of the source's live request with the same intent.
    Superseded,
    // An Impulse already issued this tick by this source for this intent; the
    // existing record's id is returned.
    Deduplicated,
    Rejected,
};

struct AnimRequestResult
{
    AnimRequestStatus Status = AnimRequestStatus::Rejected;
    AnimRequestId Id;
    AnimRejectReason Reject = AnimRejectReason::None;

    [[nodiscard]] bool Accepted() const { return Status != AnimRequestStatus::Rejected; }
    friend bool operator==(const AnimRequestResult&, const AnimRequestResult&) = default;
};

// Whether the record is live at `now`: occupied, not cancelled, and not past a
// Fixed or Impulse lifetime.
[[nodiscard]] bool IsAnimRequestLive(const AnimRequest& request, AnimTick now);

// Whether the record was live at `at` by its own ticks: started, within its
// lifetime, and not yet cancelled then. What a replay of a past tick asks; a
// record cancelled since still answers for the ticks before its cancel.
[[nodiscard]] bool WasAnimRequestLive(const AnimRequest& request, AnimTick at);

// Whether the record still belongs in the set at `now`: live, or cancelled with
// a tail that has not ended.
[[nodiscard]] bool IsAnimRequestRetained(const AnimRequest& request, AnimTick now);

void PruneAnimRequests(AnimRequestSet& set, AnimTick now, AnimDecisionLog* log);

[[nodiscard]] AnimRequestResult IssueAnimRequest(AnimRequestSet& set,
                                                 const AnimRequestDesc& desc,
                                                 AnimTick now,
                                                 AnimDecisionLog* log);

// Cancels a live request. False when the id names no live record. The record
// stays, with its reason, for the cancel tick -- which is when
// ReqCancelReason reports it -- and for as long as its tail is extended.
bool CancelAnimRequest(AnimRequestSet& set,
                       AnimRequestId id,
                       AnimCancelReason reason,
                       AnimTick now,
                       AnimDecisionLog* log);

// Keeps a cancelled request in the set until `tick`: a latch finishing its
// content, a cancel section playing out. The tail a late joiner needs to see.
void ExtendAnimRequestTail(AnimRequestSet& set, AnimRequestId id, AnimTick tick);

// The record ReqAge, ReqParam and ReqCancelReason bind to for an intent: the
// newest StartTick among live records whose layers intersect `layers`, ties to
// the higher sequence, so every machine picks the same one. Null when none.
[[nodiscard]] const AnimRequest* FindPrimaryAnimRequest(const AnimRequestSet& set,
                                                        GameplayTagId intent,
                                                        AnimTick now,
                                                        std::uint8_t layers = kAnimAllLayers);

// The reason an intent's request was cancelled on `now`, or None. Visible for
// exactly the cancel tick.
[[nodiscard]] AnimCancelReason FindAnimCancelReason(const AnimRequestSet& set,
                                                    GameplayTagId intent,
                                                    AnimTick now);

// The gameplay-facing forms: find the entity's request set and decision log,
// and refuse an intent the entity's rig does not declare. An entity without a
// request set rejects as Malformed.
[[nodiscard]] AnimRequestResult IssueAnimRequest(World& world,
                                                 EntityId animated,
                                                 const AnimRequestDesc& desc,
                                                 AnimTick now);
bool CancelAnimRequest(World& world,
                       EntityId animated,
                       AnimRequestId id,
                       AnimCancelReason reason,
                       AnimTick now);
