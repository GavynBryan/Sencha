#pragma once

#include <input/InputAction.h>
#include <input/InputActionRegistry.h>
#include <math/Vec.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// InputActionState
//
// What gameplay reads. Two views over the same actions:
//
//   Frame() -- resolved once per rendered frame, for menus, cameras, and
//              anything else that lives on the presentation clock.
//   Tick()  -- resolved once per fixed tick, for the simulation.
//
// Tick records are kept for the last few ticks as flat, tick-stamped value
// arrays indexed by action. That is deliberately the shape a player command
// wants: a network layer projects the actions it cares about out of a record
// by index, with no strings, no pointers, and no platform types anywhere in it.
//=============================================================================

// A reader over one resolved snapshot. Cheap to copy; valid until the next
// resolve pass for that clock.
struct InputActionView
{
    std::span<const InputActionValue> Values;

    [[nodiscard]] InputActionValue Get(InputActionId action) const
    {
        const std::size_t index = InputActionRegistry::IndexOf(action);
        if (!action.IsValid() || index >= Values.size())
            return InputActionValue{};
        return Values[index];
    }

    [[nodiscard]] bool Held(InputActionId action) const { return Get(action).IsHeld(); }
    [[nodiscard]] bool Pressed(InputActionId action) const { return Get(action).WasPressed(); }
    [[nodiscard]] bool Released(InputActionId action) const { return Get(action).WasReleased(); }
    [[nodiscard]] float Axis(InputActionId action) const { return Get(action).X; }

    [[nodiscard]] Vec2d Axis2(InputActionId action) const
    {
        const InputActionValue value = Get(action);
        return Vec2d(value.X, value.Y);
    }
};

struct InputActionTickRecord
{
    std::uint64_t Tick = 0;
    std::span<const InputActionValue> Values;

    [[nodiscard]] InputActionView View() const { return InputActionView{ Values }; }
};

class InputActionState
{
public:
    // Sizes the storage to an action count. Changing the count clears the
    // history: records from a different action vocabulary cannot be indexed by
    // the new one.
    void Configure(std::size_t actionCount);

    [[nodiscard]] std::size_t ActionCount() const { return Actions; }

    [[nodiscard]] std::span<InputActionValue> FrameStorage();
    [[nodiscard]] InputActionView Frame() const;

    // Opens a record for `tick` and returns the storage a resolve pass fills.
    [[nodiscard]] std::span<InputActionValue> BeginTick(std::uint64_t tick);

    [[nodiscard]] InputActionView Tick() const;
    [[nodiscard]] std::uint64_t CurrentTick() const { return NewestTick; }

    // Recent tick records, newest first, for a command builder. Older than
    // kHistoryCapacity ticks is gone.
    [[nodiscard]] std::size_t HistoryCount() const { return Recorded; }
    [[nodiscard]] InputActionTickRecord History(std::size_t indexFromNewest) const;

    // The most recent binding failure, if any, so a diagnostic surface can show
    // why controls are not behaving rather than leaving the player guessing.
    void SetError(std::string error) { LastError = std::move(error); }
    [[nodiscard]] const std::string& Error() const { return LastError; }

    static constexpr std::size_t kHistoryCapacity = 8;

private:
    std::size_t Actions = 0;
    std::vector<InputActionValue> FrameValues;
    // kHistoryCapacity records laid end to end. Sized once in Configure so the
    // spans handed out stay valid.
    std::vector<InputActionValue> RingValues;
    std::vector<std::uint64_t> RingTicks;
    std::size_t Newest = 0;
    std::size_t Recorded = 0;
    std::uint64_t NewestTick = 0;
    std::string LastError;
};
