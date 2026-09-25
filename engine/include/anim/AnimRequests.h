#pragma once

#include <anim/AnimDecisionLog.h>
#include <anim/AnimRequestSet.h>

#include <array>
#include <cstdint>

class World;

struct AnimRequestDesc
{
    EntityId Source;
    GameplayTagId Intent;
    GameplayTagId SourceTag;
    std::uint8_t Layers = kAnimAllLayers;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 0;
    std::array<std::uint32_t, kAnimRequestParams> Params{};
    // Which params are gameplay tag ids; the World form sets it from the request schema.
    std::uint8_t TagParams = 0;
};

enum class AnimRequestStatus : std::uint8_t
{
    Accepted,
    // Accepted in place of the source's live request with the same intent.
    Superseded,
    // An Impulse this source already issued this tick for this intent; returns its id.
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

// Occupied, not cancelled, and not past a Fixed or Impulse lifetime.
[[nodiscard]] bool IsAnimRequestLive(const AnimRequest& request, AnimTick now);

// Live at `at` by the record's own ticks, so a replay of a past tick still sees a
// record cancelled since.
[[nodiscard]] bool WasAnimRequestLive(const AnimRequest& request, AnimTick at);

// Live, or cancelled with a tail that has not ended.
[[nodiscard]] bool IsAnimRequestRetained(const AnimRequest& request, AnimTick now);

void PruneAnimRequests(AnimRequestSet& set, AnimTick now, AnimDecisionLog* log);

[[nodiscard]] AnimRequestResult IssueAnimRequest(AnimRequestSet& set,
                                                 const AnimRequestDesc& desc,
                                                 AnimTick now,
                                                 AnimDecisionLog* log);

// False when `id` names no live record. The record stays, with its reason, for
// the cancel tick and while its tail is extended.
bool CancelAnimRequest(AnimRequestSet& set,
                       AnimRequestId id,
                       AnimCancelReason reason,
                       AnimTick now,
                       AnimDecisionLog* log);

// Keeps a cancelled request until `tick` (a latch finishing, a cancel section
// playing out) so a late joiner still sees it.
void ExtendAnimRequestTail(AnimRequestSet& set, AnimRequestId id, AnimTick tick);

// The record ReqAge, ReqParam and ReqCancelReason read for an intent: the newest
// StartTick among live records on `layers`, ties to the higher sequence.
[[nodiscard]] const AnimRequest* FindPrimaryAnimRequest(const AnimRequestSet& set,
                                                        GameplayTagId intent,
                                                        AnimTick now,
                                                        std::uint8_t layers = kAnimAllLayers);

// Visible for exactly the cancel tick; None otherwise.
[[nodiscard]] AnimCancelReason FindAnimCancelReason(const AnimRequestSet& set,
                                                    GameplayTagId intent,
                                                    AnimTick now);

// Refuses an intent the entity's rig does not declare. An entity without a
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
