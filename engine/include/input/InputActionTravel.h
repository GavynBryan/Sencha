#pragma once

#include <input/InputAction.h>
#include <math/Vec.h>

class InputActionState;
struct InputActionView;

// A resolved axis mixes two kinds of contribution: displacement a pointer
// delivered this frame or tick, which is already a travel, and a rate a stick
// is holding, which becomes a travel only over a duration. The published value
// is their sum and the sampled share is published beside it; reading the sum as
// a per-frame travel makes a held stick turn faster the higher the frame rate.
//
// Every consumer that turns something by an axis reads one of these. The look
// integration is the engine's own consumer; a game's camera is the other.
struct InputAxis2Travel
{
    Vec2d Displacement; // this frame's or tick's pointer travel
    Vec2d Rate;         // the stick's held rate, per second
};

[[nodiscard]] InputAxis2Travel SplitAxis2(const InputActionView& value,
                                          const InputActionView& sampled,
                                          InputActionId action);

// Displacement plus rate over `seconds`.
[[nodiscard]] Vec2d Integrate(const InputAxis2Travel& travel, double seconds);

// The travel of `action` on the presentation clock over this frame's wall time,
// and on the simulation clock over this tick.
[[nodiscard]] Vec2d FrameAxis2Travel(const InputActionState& state, InputActionId action,
                                     double frameSeconds);
[[nodiscard]] Vec2d TickAxis2Travel(const InputActionState& state, InputActionId action,
                                    double tickSeconds);
