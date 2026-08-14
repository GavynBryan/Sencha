#pragma once

#include <net/ReplicationSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class ReplicationChangeStore;
class ReplicationClientIdentity;
class ReplicationInterpolation;
class World;

//=============================================================================
// Desync detection
//
// Two machines that should hold the same value, asked whether they do, without
// sending the value. The authority folds what it believes a peer holds into a
// hash per entity; the peer folds its own copy the same way and says so when
// they differ.
//
// It exists to catch replication defects and nondeterminism regressions in
// development and soak, not to police cheaters -- a client that lies about its
// hash has only lied about its own diagnostics.
//
// Two rules keep it from reporting disagreement that is not disagreement, and
// without either one it fires constantly and gets turned off, which is worse
// than not having it:
//
// - **Only entities the peer has fully proved.** A client's view is a mix of
//   generations by design: an entity is omitted when unchanged and deferred
//   when the budget is full, so "the world at tick T" is something no client
//   ever holds. Only an entity whose every field run was already confirmed at
//   that peer's floor is one the two sides should agree about completely.
// - **Only fields that peer was eligible to receive.** An owner-only field
//   withheld from a non-owner is a field both sides are correct to disagree
//   about. The fold takes the same mask the writer did.
//
// Predicted components are excluded on the entity a peer drives, for the same
// reason one step further out: that machine is deliberately ahead of the
// authority on them, and being ahead is the feature. Only on that entity,
// though -- everywhere else a predicted component is ordinary replicated state
// that lands in the world like any other, and since the transform is predicted,
// skipping it everywhere would leave this comparing almost nothing.
//=============================================================================

struct NetDesyncSample
{
    NetEntityId Id;
    std::uint64_t Hash = 0;
};

// How many entities one report carries. Small on purpose: this rides the
// unreliable channel beside snapshots on a dev-only cadence, and a report that
// competed with state for the budget would be a diagnostic that caused the
// problem it looks for. Coverage comes from rotating through the world across
// reports rather than from one large one.
inline constexpr std::size_t kNetMaxDesyncSamples = 16;

[[nodiscard]] std::size_t NetEncodeDesyncReport(
    std::uint64_t tick, std::span<const NetDesyncSample> samples,
    std::span<std::byte> out);

[[nodiscard]] bool NetDecodeDesyncReport(std::span<const std::byte> bytes,
                                         std::uint64_t& tick,
                                         std::vector<NetDesyncSample>& samples);

// Authority: the next few entities this peer has fully proved, folded as that
// peer should hold them.
//
// `cursor` walks the change store between calls so successive reports cover
// different entities; it is this peer's, and the caller keeps it. Fills
// `samples`, replacing what it held.
void NetBuildDesyncReport(const ReplicationChangeStore& changes,
                          const ReplicationLayout& layout,
                          const ReplicationPeerState& peer,
                          std::uint32_t ownerPeer, std::size_t& cursor,
                          std::vector<NetDesyncSample>& samples);

// Client: what this machine holds for one entity, folded the same way.
//
// `interpolation` supplies the authority's last word on a pose rather than the
// blend currently on the entity -- a mirrored entity's transform is a presented
// value between two authoritative ones, and comparing that would report every
// moving object as divergent. Null on a machine that does not interpolate.
//
// Returns false when the entity is not held here at all, which is its own kind
// of answer.
[[nodiscard]] bool NetFoldLocalEntity(const World& world,
                                      const ReplicationLayout& layout,
                                      const ReplicationClientIdentity& identity,
                                      const ReplicationInterpolation* interpolation,
                                      std::uint32_t selfPeer, NetEntityId id,
                                      std::uint64_t& hash);

struct NetDesyncResult
{
    std::uint32_t Compared = 0;
    // Entities this machine does not hold at all. Not a divergence: a report
    // can name an entity a snapshot has not delivered yet.
    std::uint32_t Absent = 0;
    std::uint32_t Diverged = 0;
    // The first one that differed, for the log line. Which component and which
    // value is `net_entity <netid>`'s question, and it already answers it with
    // the writer's own owed-field rule.
    NetEntityId FirstDiverged;
};

// Client: compares a report against this machine's own state.
[[nodiscard]] NetDesyncResult NetCheckDesyncReport(
    const World& world, const ReplicationLayout& layout,
    const ReplicationClientIdentity& identity,
    const ReplicationInterpolation* interpolation, std::uint32_t selfPeer,
    std::span<const NetDesyncSample> samples);
