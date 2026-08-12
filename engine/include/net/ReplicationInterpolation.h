#pragma once

#include <app/GameContexts.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <net/ClientPrediction.h>
#include <net/NetTickEstimator.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// ReplicationInterpolation
//
// The entities a client mirrors rather than simulates, drawn along the path the
// authority described instead of wherever the last datagram happened to leave
// them.
//
// This is the other half of ClientPrediction. A client's own pawn cannot wait
// for a round trip, so it is simulated locally and corrected; everything else
// can wait, and waiting deliberately is what makes it smooth. State arrives when
// datagrams do -- in bursts, late, out of order, and sometimes not at all --
// while ticks come at a fixed rate. Writing each snapshot's pose the moment it
// lands hands that irregularity straight to the screen: a pawn that does not
// move on a tick whose snapshot was late, and then covers two ticks of ground
// when it arrives.
//
// So poses are held with the authority tick they describe, and each tick
// presents the pose for a tick slightly in the past -- far enough back that the
// samples bracketing it have already arrived. Between two samples the pose is
// interpolated, which is what makes a lost snapshot cost nothing visible: the
// motion either side of it still describes the path through it.
//
// The delay is the whole cost, and it is only paid by things this machine is not
// simulating. It buys back exactly what jitter takes.
//
// Never extrapolates. Past the newest sample the pose holds, because a guess
// about where something went is a guess that has to be taken back, and taking it
// back is a visible snap in the opposite direction. Holding still is wrong in a
// way that resolves itself.
//
// Pure: no World, no session, no clock. Poses and ticks go in, a pose comes out.
//=============================================================================
class ReplicationInterpolation
{
public:
    // Poses kept per entity. Enough to bracket a presentation tick on a
    // connection whose datagrams arrive several ticks apart; past this the
    // oldest is forgotten, which is the right thing to forget.
    static constexpr std::size_t kSamples = 8;

    // Jitter margin, in ticks, on top of the flight time the clock already
    // knows about AND the interval between snapshots. Two ticks absorbs
    // ordinary jitter at a cost most players cannot see.
    //
    // Margin rather than absolute delay because the two terms answer different
    // questions. The interval says how old the newest sample that can possibly
    // have arrived is -- publishing every third tick means the newest one is up
    // to three ticks behind before the network is involved -- and presenting
    // ahead of it holds the last pose instead of blending, which is the stutter
    // this class exists to remove. That term is arithmetic. This one is taste.
    static constexpr std::uint32_t kDefaultDelayTicks = 2;

    // Whether an arriving component is this machine's to present rather than the
    // world's to hold. Position is what a viewer sees move; everything else
    // about a mirrored entity still lands normally.
    [[nodiscard]] bool Intercepts(ComponentTypeId type) const;

    // Off writes arriving poses straight to the world, which is the stepping
    // behaviour this replaced. Kept so the difference can be seen rather than
    // argued about.
    void SetEnabled(bool enabled) { Enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return Enabled; }

    void SetDelayTicks(std::uint32_t ticks) { Delay = ticks; }
    [[nodiscard]] std::uint32_t DelayTicks() const { return Delay; }

    // Ticks between the authority's snapshots, which the authority owns and
    // tells this machine. Absorbed into the presented tick so a lower snapshot
    // rate costs a little more age rather than a held pose every few frames.
    void SetSnapshotInterval(std::uint32_t ticks)
    {
        SnapshotInterval = std::max<std::uint32_t>(1, ticks);
    }
    [[nodiscard]] std::uint32_t SnapshotIntervalTicks() const
    {
        return SnapshotInterval;
    }

    // What the two terms come to: how far behind the estimated authority tick a
    // mirrored entity is drawn, before flight time.
    [[nodiscard]] std::uint32_t PresentationLagTicks() const
    {
        return SnapshotInterval + Delay;
    }

    // What the authority said this entity's pose was at that tick. Samples older
    // than the window, and ticks already held, are ignored -- the redundancy in
    // the stream means the same tick arrives more than once.
    void Record(EntityId entity, std::uint64_t authorityTick,
                const LocalTransform& pose);

    //-------------------------------------------------------------------------
    // The authority's own view, per entity
    //
    // Kept apart from what gets presented, for the same reason the predictor
    // keeps one. A snapshot carries only what changed since the authority's
    // baseline, so a delta has to be applied to what the authority believes it
    // sent. Applied to the presented pose instead, a field it had no reason to
    // resend would be filled in from a blend this machine invented between two
    // ticks -- and every later delta would compound the difference.
    //
    // This is load-bearing today, not a precaution. LocalTransform is one
    // schema field, but the wire does not see schema fields -- it sees the runs
    // the flattener produces, and it produces three (position, rotation,
    // scale), each with its own mask bit. A rotating entity that is not moving
    // sends rotation alone, so partial transform deltas are the ordinary case.
    // Staged against the presented pose, that delta would land on a blend this
    // machine invented, and every later delta would compound it.
    //-------------------------------------------------------------------------
    [[nodiscard]] std::span<std::byte> AuthoritativeBytes(EntityId entity);
    [[nodiscard]] bool HasAuthoritativeState(EntityId entity) const;

    // Takes whatever was decoded into AuthoritativeBytes as this entity's pose
    // at that tick.
    void Commit(EntityId entity, std::uint64_t authorityTick);

    // The pose to hold at `presentTick`, or nothing when this entity has said
    // nothing that can answer for it. Interpolates between the samples either
    // side; holds the nearest one when the tick falls outside them, because
    // guessing past the newest sample is a guess that gets taken back.
    [[nodiscard]] std::optional<LocalTransform> Resolve(
        EntityId entity, std::uint64_t presentTick) const;

    // Everything currently tracked, into storage the caller keeps. Filling a
    // caller's vector rather than returning one because this is read every tick,
    // and a fresh allocation per tick is a cost with nothing to show for it.
    void Tracked(std::vector<EntityId>& out) const;
    [[nodiscard]] std::size_t TrackedCount() const { return Entities.size(); }

    // An entity that is gone, or that this machine has taken over simulating.
    // Samples for one entity say nothing about another, and a recycled handle is
    // a different entity.
    void Forget(EntityId entity);
    void Reset();

    // What the stats surface reads. Held ticks are the ones where the presented
    // pose ran past the newest sample and stopped: the visible symptom of a
    // stream that has fallen behind the delay it was given.
    [[nodiscard]] std::uint64_t Resolved() const { return ResolvedTicks; }
    [[nodiscard]] std::uint64_t Held() const { return HeldTicks; }
    [[nodiscard]] std::uint64_t Interpolated() const { return InterpolatedTicks; }

private:
    struct Sample
    {
        std::uint64_t Tick = 0;
        LocalTransform Pose;
        bool Filled = false;
    };

    // Sorted oldest first. A ring rather than a map: the window is tiny and
    // fixed, and samples arrive in order almost every time.
    struct Track
    {
        std::array<Sample, kSamples> Samples{};
        std::size_t Count = 0;
        // What the authority last said, which is what its deltas are against.
        LocalTransform Authoritative{};
        bool SeenAuthority = false;
    };

    std::unordered_map<EntityId, Track, EntityIdHash> Entities;
    bool Enabled = true;
    std::uint32_t Delay = kDefaultDelayTicks;
    // What the authority says its snapshot cadence is. One until told, which is
    // the safe direction to be wrong in: too little lag holds a pose, too much
    // draws the world further into the past. The host sets it when the cvar is
    // registered and a client is told by the replicated cvar.
    std::uint32_t SnapshotInterval = 1;

    mutable std::uint64_t ResolvedTicks = 0;
    mutable std::uint64_t HeldTicks = 0;
    mutable std::uint64_t InterpolatedTicks = 0;
};

//=============================================================================
// ReplicationInterpolationSystem
//
// Puts the presented pose in the world, once per tick.
//
// On the tick clock rather than the frame clock, and ahead of propagation,
// because the pose a frame draws is derived from this one: transform
// propagation turns it into a world transform, and WorldTransformHistory blends
// between the last two of those for the fraction of a tick the frame lands on.
// Two smoothing mechanisms, one per timescale -- this one covers snapshot to
// tick, that one covers tick to frame, and neither is asked to do the other's
// job.
//
// Which tick is presented comes from the shared clock: where the authority is
// now, less the flight the clock has already measured, less the margin. The
// flight term is what puts the presented tick at the newest sample that can
// physically have arrived; the margin is what leaves its neighbours time to
// arrive too.
//=============================================================================
// Its order among the other fixed-logic systems is free, and that is a finding
// rather than an omission: it writes LocalTransform only on mirrored entities,
// which carry no MovementIntent and no character mover, so nothing else in the
// list touches them. The one ordering it does need is against transform
// propagation, and that is a frame-phase fact -- propagation runs after the
// whole fixed-logic list -- rather than something a schedule edge could state.
//
// Written down because an undeclared constraint and a nonexistent one look
// identical from outside, and the next person to add a system that reads a
// mirrored pose needs to know which this was.
class ReplicationInterpolationSystem
{
public:
    ReplicationInterpolationSystem(ReplicationInterpolation& interpolation,
                                   const ClientPrediction& prediction,
                                   const NetTickEstimator& clock)
        : Interpolation(&interpolation), Prediction(&prediction), Clock(&clock)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

private:
    ReplicationInterpolation* Interpolation = nullptr;
    const ClientPrediction* Prediction = nullptr;
    const NetTickEstimator* Clock = nullptr;
    // Reused across ticks: the tracked set is walked every tick and rebuilding
    // the list each time would allocate for nothing.
    std::vector<EntityId> Scratch;
};
