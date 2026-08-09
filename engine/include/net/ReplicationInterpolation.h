#pragma once

#include <app/GameContexts.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <net/ClientPrediction.h>
#include <net/NetTickEstimator.h>
#include <world/transform/TransformComponents.h>

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

    // Ticks behind the newest expected sample that the presented pose sits, on
    // top of the flight time the clock already knows about. Two ticks of margin
    // absorbs ordinary jitter at a cost most players cannot see.
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
    // No test can currently show that going wrong, and it is worth saying why
    // rather than leaving the impression one does. LocalTransform is a single
    // schema field, so its mask is one bit and the transform arrives whole or
    // not at all; there is no partial delta to stage wrongly. That stops being
    // true the moment position and rotation are separated, which is what
    // per-field quantization would want. Staging against the world is wrong in
    // a way that would be silent then, so it is not done now.
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
