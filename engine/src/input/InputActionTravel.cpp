#include <input/InputActionTravel.h>

#include <input/InputActionState.h>

InputAxis2Travel SplitAxis2(const InputActionView& value, const InputActionView& sampled,
                            InputActionId action)
{
    InputAxis2Travel travel;
    const Vec2d sum = value.Axis2(action);
    travel.Rate = sampled.Axis2(action);
    travel.Displacement = Vec2d(sum.X - travel.Rate.X, sum.Y - travel.Rate.Y);
    return travel;
}

Vec2d Integrate(const InputAxis2Travel& travel, double seconds)
{
    return Vec2d(static_cast<float>(travel.Displacement.X + travel.Rate.X * seconds),
                 static_cast<float>(travel.Displacement.Y + travel.Rate.Y * seconds));
}

Vec2d FrameAxis2Travel(const InputActionState& state, InputActionId action,
                       double frameSeconds)
{
    return Integrate(SplitAxis2(state.Frame(), state.FrameSampled(), action), frameSeconds);
}

Vec2d TickAxis2Travel(const InputActionState& state, InputActionId action,
                      double tickSeconds)
{
    return Integrate(SplitAxis2(state.Tick(), state.TickSampled(), action), tickSeconds);
}
