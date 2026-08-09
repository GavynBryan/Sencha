#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>

class CharacterMoverPool;
class ClientPrediction;
class World;
class WorldComponentSchema;

//=============================================================================
// Putting a client's own pawn back in step with the authority.
//
// Not by nudging it toward where the authority said it was. By starting again
// from what the authority actually did, and re-running the ticks it has not
// answered yet.
//
// The difference is not a refinement. An offset assumes the two machines
// disagree by a displacement and that shifting one of them fixes it -- but a
// pawn put in the right place still carrying the wrong velocity walks straight
// back out, and one that disagrees about what it is standing on disagrees about
// everything that follows. Errors of that kind compound: each correction is
// computed from a state that is already wrong in a way the correction does not
// describe. That is how a pawn ends up wedged on a corner on one machine and
// running down a hallway on the other, with both of them still exchanging
// datagrams and neither able to close the gap.
//
// Replaying instead makes divergence impossible to accumulate. The client's
// state after every snapshot is a function of the authority's state and the
// client's own unanswered input -- both of which the two machines agree on --
// so there is no place for a difference to hide and grow. What is left is the
// residual of one tick, re-derived every time rather than carried.
//
// It runs where snapshots are applied, before any tick, and mutates no
// archetypes: it writes component values and sweeps one character. That is why
// jump had to leave the ability layer, and why nothing here may grow a
// dependency on spawning an entity to say something happened.
//=============================================================================

struct PawnReplayRequest
{
    World* Entities = nullptr;
    const WorldComponentSchema* Schema = nullptr;
    ClientPrediction* Prediction = nullptr;
    // Where characters actually live. Null in a configuration with no physics,
    // where the transform is all there is to restore.
    CharacterMoverPool* Movers = nullptr;

    // The newest command tick the authority has finished with. Everything the
    // client kept above it is what this owes.
    std::uint64_t AckTick = 0;

    float FixedDeltaSeconds = 1.0f / 60.0f;
    Vec3d Gravity{ 0.0f, -9.81f, 0.0f };
    Vec3d UpAxis{ 0.0f, 1.0f, 0.0f };

    // False leaves the pawn where the authority put it without re-running
    // anything -- the input-delay behaviour, which costs a round trip on every
    // input and is kept so the difference can be seen rather than argued about.
    bool Replay = true;
};

struct PawnReplayResult
{
    bool Ran = false;
    std::uint32_t TicksReplayed = 0;
    // How far the pawn moved between where this machine had it and where
    // replaying from the authority's state put it. The number to watch: a
    // steady few centimetres is a healthy connection, a rising floor is the two
    // simulations drifting apart faster than they are being pulled together.
    float ResetMeters = 0.0f;
    // The authority's word could not be replayed onto -- a stall longer than
    // the ring, or rules this machine does not implement -- so the pawn was
    // moved outright instead. Honest, and visible.
    bool Snapped = false;
};

// Restores the pawn to the authority's last word and re-runs everything since.
[[nodiscard]] PawnReplayResult ReplayPawnState(const PawnReplayRequest& request);
