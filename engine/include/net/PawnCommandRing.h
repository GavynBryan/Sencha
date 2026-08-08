#pragma once

#include <input/InputAction.h>
#include <movement/MovementIntent.h>
#include <net/NetPlayerCommand.h>

#include <array>
#include <cstddef>
#include <cstdint>

//=============================================================================
// PawnCommandRing
//
// Every tick this client has simulated for its own pawn and not yet had
// answered.
//
// One record per tick, kept under the name the authority will know that tick
// by, holding everything the tick's movement depended on: where the player was
// aiming, what the input came to, and the raw actions the authority re-derives
// its own answer from.
//
// It is one ring rather than three sources on purpose. The client's own
// simulation, the command that goes on the wire, and the replay after a
// correction all have to be the same input, or the two machines are answering
// different questions and the difference reads as prediction error. Sampling
// each of them separately is what made aim arrive per datagram while movement
// arrived per tick.
//
// Sixty-four ticks, which is a second at sixty. A connection further behind
// than that has bigger problems than a smooth correction, and the replay says
// so by refusing rather than guessing.
//=============================================================================

struct PawnCommandTick
{
    // On the authority's clock, decided when the record was captured. This is
    // the name both machines use for the tick, and what the authority's
    // acknowledgement refers to.
    std::uint64_t Tick = 0;

    // Where the player was aiming as this tick simulated. Carried per tick
    // because movement is derived from it: a tick replayed against a different
    // aim walks a different path, and on a corner that is the difference
    // between stopping and not.
    float Yaw = 0.0f;
    float Pitch = 0.0f;

    // What the tick's input came to. This is what replay feeds the movement
    // step; the actions below are what the authority derives its own copy from.
    MovementIntent Intent{};

    std::uint8_t ActionCount = 0;
    std::array<InputActionValue, kNetMaxCommandActions> Actions{};
};

class PawnCommandRing
{
public:
    static constexpr std::size_t kDepth = 64;

    void Push(const PawnCommandTick& record);
    void Clear();

    // Drops everything the authority has confirmed it simulated. What is left
    // is exactly what a replay owes.
    void DropThrough(std::uint64_t tick);

    // Oldest first, for replay. False when the span cannot be replayed
    // faithfully: a tick the authority has not confirmed has already been
    // pushed out of the window, so replaying what remains would skip input it
    // is about to apply.
    //
    // An acknowledgement far below what is held is not a gap by itself -- at
    // the start of a session nothing has been confirmed and every record ever
    // captured is still here. What makes a gap is having lost one.
    template <typename Fn>
    [[nodiscard]] bool ForEachAfter(std::uint64_t tick, Fn&& visit) const
    {
        if (tick < LostThrough)
            return false;

        for (std::size_t index = 0; index < Count; ++index)
        {
            const PawnCommandTick& record = At(index);
            if (record.Tick <= tick)
                continue;
            visit(record);
        }
        return true;
    }

    [[nodiscard]] std::size_t Size() const { return Count; }
    [[nodiscard]] std::uint64_t NewestTick() const { return Newest; }
    // Newest first, which is the order the wire wants and the order the encoder
    // drops from when the window does not fit.
    [[nodiscard]] const PawnCommandTick& Recent(std::size_t index) const;

private:
    [[nodiscard]] const PawnCommandTick& At(std::size_t index) const
    {
        return Records[(Head + index) % kDepth];
    }

    std::array<PawnCommandTick, kDepth> Records{};
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::uint64_t Newest = 0;
    // The newest tick this ring has had to overwrite. An acknowledgement at or
    // above it means nothing owed has been lost; below it, something has.
    std::uint64_t LostThrough = 0;
};
