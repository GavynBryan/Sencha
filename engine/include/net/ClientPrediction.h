#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

//=============================================================================
// ClientPrediction
//
// The one entity a client simulates for itself, and how it is put back in step
// with the authority.
//
// Everything else a client holds is a puppet: it arrives as state and is drawn
// where the authority last said. Its own pawn cannot be, because the player is
// holding the key now and would watch the reply arrive. So the client runs the
// same systems over the same input and moves immediately, and the authority's
// word becomes something to check against rather than something to obey.
//
// Correction is by offset, not by teleport. When the authority's position for
// tick T differs from what this machine predicted for T, everything simulated
// since T was built on that same error, so the whole trajectory is shifted by
// it -- the pawn lands where the authority says it was without discarding the
// motion the player has produced in the meantime. Snapping to the authority's
// stale position instead would drag the player backwards by the round trip on
// every correction.
//
// This is deliberately not rollback. Rollback would re-run the ticks since T
// from the recorded input, which needs a way to run a tick for one entity, and
// Sencha's schedule has no such seam -- adding one touches every gameplay
// system. The residual is that a correction is approximate where a replay would
// be exact, which shows up under loss and against hard collisions. The history
// and comparison here are what a replay would need anyway, so this is the first
// half of that work rather than a detour around it.
//
// Pure: no World, no session, no clock. Positions and ticks go in, a correction
// comes out.
//=============================================================================
class ClientPrediction
{
public:
    // A second of history at sixty ticks. An authority further behind than this
    // is a connection whose corrections are worthless anyway.
    static constexpr std::size_t kHistory = 64;

    // Beyond this the prediction is not worth blending out and the pawn is
    // moved at once. Well past ordinary disagreement, which is centimetres.
    static constexpr float kDefaultSnapMeters = 2.0f;

    // Under this the disagreement is quantization and float noise rather than a
    // divergence, and correcting it would jitter a pawn that is already right.
    static constexpr float kIgnoreMeters = 0.01f;

    // Which entity this machine simulates rather than mirrors. Invalid means
    // none, which is every client before it has been given a pawn and every
    // process that is not a client at all.
    void SetPredicted(EntityId entity) { Subject = entity; }
    [[nodiscard]] EntityId Predicted() const { return Subject; }
    [[nodiscard]] bool Predicts(EntityId entity) const
    {
        return Subject.IsValid() && entity == Subject;
    }

    // Whether an arriving component belongs to the authority's argument with
    // this machine rather than to the world. Position is what a player feels
    // and what this predicts; everything else about a predicted entity still
    // lands normally.
    [[nodiscard]] bool Intercepts(EntityId entity, ComponentTypeId type) const;

    // Off means the authority's position lands in the world as it does for any
    // other entity, which is the input-delay behaviour prediction replaced.
    // Local simulation still runs and is simply overwritten -- the cost of
    // proving what prediction is worth is a wasted tick, not a second code path.
    void SetEnabled(bool enabled) { Enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return Enabled; }

    // Where this machine believes the pawn was at an authority tick. Recorded
    // after the tick has been simulated, once per tick.
    void Record(std::uint64_t authorityTick, const Vec3d& position);

    //-------------------------------------------------------------------------
    // The authority's own view
    //
    // Kept apart from the world's copy, which holds what this machine
    // simulated. A snapshot carries only what changed since the authority's
    // baseline, so a delta has to be applied to what the authority believes it
    // sent. Applied to the predicted value instead, an unchanged field would be
    // filled in from this machine's own guess -- and a divergence in exactly
    // the field the authority had no reason to resend would read as perfect
    // agreement.
    //-------------------------------------------------------------------------
    [[nodiscard]] std::span<std::byte> AuthoritativeBytes();
    [[nodiscard]] bool HasAuthoritativeState() const { return SeenAuthority; }

    // What the authority says the pawn was at that tick.
    struct Correction
    {
        // Added to the pawn's current position, not assigned to it: the motion
        // simulated since the tick keeps its shape and moves with it.
        Vec3d Offset;
        float Distance = 0.0f;
        // Far enough that blending it out would look worse than moving.
        bool Snap = false;
    };

    // Adopts whatever was decoded into AuthoritativeBytes and compares it
    // against what was predicted for that tick.
    //
    // Nothing when the tick predates the history, when nothing was predicted
    // for it, or when the disagreement is beneath noticing -- all three are
    // ordinary, and none of them is a correction.
    [[nodiscard]] std::optional<Correction> Commit(std::uint64_t authorityTick);

    void SetSnapMeters(float meters) { SnapMeters = meters; }
    [[nodiscard]] float SnapMeters_() const { return SnapMeters; }

    // What the stats surface reads. Error is the last disagreement seen,
    // including the ones too small to correct, because a rising floor is the
    // early warning that prediction is drifting.
    [[nodiscard]] float LastErrorMeters() const { return LastError; }
    [[nodiscard]] std::uint64_t Observations() const { return Observed; }
    [[nodiscard]] std::uint64_t Corrections() const { return Corrected; }
    [[nodiscard]] std::uint64_t Snaps() const { return Snapped; }

    // Session-transient, and also cleared when the predicted pawn changes:
    // history recorded for one entity says nothing about another.
    void Reset();

private:
    struct Sample
    {
        std::uint64_t Tick = 0;
        Vec3d Position;
        bool Filled = false;
    };

    // Indexed by tick, so a lookup is arithmetic rather than a search and an
    // overwritten slot retires itself.
    std::array<Sample, kHistory> Samples{};

    // What the authority last said, which is what its deltas are against.
    LocalTransform Authoritative{};
    bool SeenAuthority = false;

    EntityId Subject;
    bool Enabled = true;
    float SnapMeters = kDefaultSnapMeters;

    float LastError = 0.0f;
    std::uint64_t Observed = 0;
    std::uint64_t Corrected = 0;
    std::uint64_t Snapped = 0;
};
