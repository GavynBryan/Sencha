#pragma once

#include <math/Vec.h>

#include <cstdint>
#include <string_view>
#include <vector>

class CharacterMoverPool;
class ClientPrediction;
class ReplicationLayout;
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
//
// Why this is not in net/. Everything it writes is simulation state, and
// everything it needs from netcode -- the authority's last word, which ticks
// have been answered, which are still owed -- it reads as data. A replay driven
// from inside net would have the transport layer calling movement and physics,
// which is the arrow pointing the wrong way; the same code one layer up calls
// both by name and owes nothing back. So net stays what it should be: the wire,
// the codec, the clocks, and an honest record of what the authority has and has
// not answered.
//
// What one re-run tick *does* is movement's own (movement/CharacterTickStep.h).
// This decides which ticks to run and what to do when they cannot be; a second
// implementation of the tick would reintroduce, as a difference between two
// codebases, exactly the divergence replaying exists to remove.
//
// The shape here is a character's, deliberately and all the way through: the
// request carries gravity, an up axis, and a mover pool, and the loop steps a
// character. A second thing worth replaying -- a vehicle, a projectile the
// firer simulates ahead -- is a second request type and a second loop beside
// this one, not a widening of these. Their only common part is "restore, then
// re-run the unanswered ticks", which is four lines and shares no state.
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

    // The values the scheduled tick integrates under, which a caller reads off
    // the movement system that owns them rather than restating here. The
    // defaults exist so a test rig on engine defaults is not obliged to say so;
    // a caller that leaves them while the simulation runs on something else is
    // replaying under different physics than it is correcting.
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

// Every component the replication table declares Predicted that re-running a
// tick will not put back in step, by name, in table order. Empty is the healthy
// answer and the one this build gives.
//
// Predicted is a real declaration with a real effect: the applier holds what
// arrives for that component apart from the world's copy instead of overwriting
// the value its owner is still simulating. What it cannot do is arrange for
// anything to re-run, because the only thing that re-runs is the character tick
// above. So a component outside that tick is restored to the authority's last
// word at every snapshot and then left there. Whatever its owner advanced in
// the ticks the authority has not answered is discarded, at the snapshot rate,
// for as long as the session lasts -- and nothing about the component's own
// behaviour looks wrong, which is why this is asked at startup rather than left
// to be noticed.
//
// Reported rather than refused. The declaration is not malformed; it is being
// asked for something this build does not do, and the useful response is to say
// which component and what will happen to it.
void CollectUnresumedPredictedComponents(const ReplicationLayout& layout,
                                         std::vector<std::string_view>& out);
