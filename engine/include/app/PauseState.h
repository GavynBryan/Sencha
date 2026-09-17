#pragma once

#include <cstdint>

class InputContextSet;
class RuntimeFrameLoop;

//=============================================================================
// PauseState
//
// Whether local play is suspended, and everything that flips with it.
//
// One transition with one owner, rather than a boolean sampled in several
// places: entering and leaving are the only two things that happen, and each
// happens all at once. What a menu is on screen, which page is on top and what
// Back means there are not here -- this drives input, the pointer and the
// simulation clock, and knows nothing about documents.
//
// Not an ECS component. Pause is a property of the session rather than of an
// entity, and a component with exactly one instance is not a component.
//=============================================================================

enum class PausePhase : std::uint8_t
{
    Playing,
    Paused,
};

// What opening the shell does to the world behind it.
enum class PausePolicy : std::uint8_t
{
    // Fixed ticks stop accruing: the single-player answer, and the one that
    // makes cooldowns, physics and animation stop with everything else.
    SuspendSimulation,
    // The world keeps running and only local controls go quiet.
    InputOnly,
};

// What Apply resolved, so a caller can act on the edge rather than poll.
enum class PauseTransition : std::uint8_t
{
    None,
    Entered,
    Left,
};

class PauseState
{
public:
    void SetPolicy(PausePolicy policy) { Policy = policy; }
    [[nodiscard]] PausePolicy GetPolicy() const { return Policy; }

    // Whether this process shares its simulation with anyone. Refreshed by
    // whoever owns the session; false for a process with none.
    void SetSessionLive(bool live) { SessionLive = live; }

    // The policy actually in force.
    //
    // A stock application-shell menu never freezes a live network session, at
    // either end. Suspension applies only where this process's simulated time
    // is nobody else's -- authority is the wrong test, because a host does own
    // the simulation and freezing it would stop the match for every player
    // while the menu claimed to be a local thing. A game that wants a host
    // pause to stop everyone sets the replicated timescale itself.
    [[nodiscard]] PausePolicy Effective() const
    {
        return SessionLive ? PausePolicy::InputOnly : Policy;
    }

    // Ask for a transition. Idempotent, and coalesced until Apply: holding a
    // key, or a focus loss arriving beside a Back press, cannot queue two.
    void Request(PausePhase phase);
    [[nodiscard]] bool HasPendingRequest() const { return Pending != Current; }

    // Resolves at most one pending transition, in whichever direction, and
    // reports what it did.
    //
    // Called from two places, each immediately after the one thing that could
    // have requested something there: the shell's PreSimulate reader, and the
    // menu's frame update. Either direction is correct at either -- the frame's
    // tick budget is already decided by the time either runs, so a frame that
    // leaves the paused state still runs no ticks and the next one resumes.
    //
    // Only an entry resolved during PreSimulate cancels the frame's remaining
    // ticks, because only there is there anything left to cancel; that is the
    // caller's call to make, which is why this reports rather than does it.
    PauseTransition Apply(RuntimeFrameLoop& runtime, InputContextSet& contexts);

    [[nodiscard]] PausePhase Phase() const { return Current; }
    [[nodiscard]] bool IsPaused() const { return Current == PausePhase::Paused; }

    // Whether the pointer must not be captured right now. A request, not a
    // command: the platform owner ands this with the game's standing intent and
    // with window focus, so resuming restores exactly the state the game asked
    // for and a game that never wanted capture does not acquire it.
    [[nodiscard]] bool SuppressesPointerCapture() const { return IsPaused(); }

private:
    PausePhase Current = PausePhase::Playing;
    PausePhase Pending = PausePhase::Playing;
    PausePolicy Policy = PausePolicy::SuspendSimulation;
    bool SessionLive = false;
    // Whether this object is the one that suspended the simulation, so leaving
    // does not clear a suspension somebody else owns.
    bool SuspendedSimulation = false;
};
