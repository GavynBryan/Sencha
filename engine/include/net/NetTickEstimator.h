#pragma once

#include <cstdint>

//=============================================================================
// NetTickEstimator
//
// Where the authority is on its own clock, as seen from here.
//
// Two machines running the same fixed step still count ticks from their own
// process start, so "tick 400" names nothing shared. Until it does, a client
// can order its own commands and nothing more: it cannot say which tick it
// wants its input applied on, cannot ask what the authority believed at a tick
// it predicted, and cannot tell early input from late.
//
// This turns the authority's tick, heard at a round trip's remove, into an
// offset from the local tick counter -- so the client keeps counting its own
// ticks and simply knows what they are called on the other machine.
//
// It slews rather than snaps. The estimate is built from a jittery round trip,
// and a stamp that jumped around would reach the authority's arrival buffer as
// alternating gaps and backlog: input lost and input late, from a client whose
// connection was merely normal. It does snap across a discontinuity, because
// slewing a second-wide correction one tick at a time would mis-stamp
// everything sent while it caught up.
//
// Pure: every input is a parameter. Clocks come from the caller, which is what
// lets a test drive a whole connection's worth of drift in a loop.
//=============================================================================
class NetTickEstimator
{
public:
    // Beyond this the difference is a discontinuity -- a join, a host hitch, a
    // route change -- rather than jitter.
    static constexpr std::int64_t kSnapTicks = 8;

    // Ticks the estimate moves per observation while slewing. One keeps the
    // stamp monotone at any plausible sample rate; faster would reintroduce
    // the jumpiness slewing exists to remove.
    static constexpr std::int64_t kSlewTicks = 1;

    // `authorityTick` is what the authority said it was at, `localTick` is
    // where this machine is now, and the round trip dates the sample. A
    // non-positive tick duration is ignored rather than dividing by it.
    void Observe(std::uint64_t authorityTick, std::uint64_t localTick,
                 std::uint64_t roundTripMicroseconds, double tickSeconds);

    // How much margin a command should carry beyond its flight time. The
    // authority's to decide, since it is the machine holding input back, and
    // it reaches here as a replicated cvar.
    void SetSlackTicks(std::uint32_t ticks) { Slack = ticks; }
    [[nodiscard]] std::uint32_t SlackTicks() const { return Slack; }

    [[nodiscard]] bool HasEstimate() const { return Observed; }

    // What the authority is simulating while this machine simulates
    // `localTick`.
    [[nodiscard]] std::uint64_t AuthorityTickAt(std::uint64_t localTick) const;

    // The tick a command sent now should be stamped for: far enough ahead that
    // it arrives before the authority reaches it, plus the slack the authority
    // asked for. Stamping the present would guarantee every command arrived
    // for a tick already simulated.
    [[nodiscard]] std::uint64_t CommandTickAt(std::uint64_t localTick) const;

    // The same thing as a number to add to any local tick, for a caller
    // stamping a window of records at once.
    [[nodiscard]] std::int64_t CommandOffset() const;

    // Ticks the authority's word takes to get here: half the round trip.
    [[nodiscard]] std::uint32_t FlightTicks() const { return Flight; }
    [[nodiscard]] std::int64_t Offset() const { return Delta; }

    void Reset();

private:
    // Authority tick minus local tick. Signed and possibly negative: which
    // machine started first is an accident.
    std::int64_t Delta = 0;
    std::uint32_t Flight = 0;
    std::uint32_t Slack = 0;
    bool Observed = false;
};
