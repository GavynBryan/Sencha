#include <input/InputActionState.h>

void InputActionState::Configure(std::size_t actionCount)
{
    if (Actions == actionCount)
        return;

    Actions = actionCount;
    FrameValues.assign(actionCount, InputActionValue{});
    RingValues.assign(actionCount * kHistoryCapacity, InputActionValue{});
    RingTicks.assign(kHistoryCapacity, 0);
    Newest = 0;
    Recorded = 0;
    NewestTick = 0;
}

std::span<InputActionValue> InputActionState::FrameStorage()
{
    return FrameValues;
}

InputActionView InputActionState::Frame() const
{
    return InputActionView{ FrameValues };
}

std::span<InputActionValue> InputActionState::BeginTick(std::uint64_t tick)
{
    if (Actions == 0)
        return {};

    Newest = Recorded == 0 ? 0 : (Newest + 1) % kHistoryCapacity;
    if (Recorded < kHistoryCapacity)
        ++Recorded;
    RingTicks[Newest] = tick;
    NewestTick = tick;
    return std::span<InputActionValue>(RingValues).subspan(Newest * Actions, Actions);
}

InputActionView InputActionState::Tick() const
{
    if (Actions == 0 || Recorded == 0)
        return InputActionView{};
    return InputActionView{
        std::span<const InputActionValue>(RingValues).subspan(Newest * Actions, Actions)
    };
}

InputActionTickRecord InputActionState::History(std::size_t indexFromNewest) const
{
    if (Actions == 0 || indexFromNewest >= Recorded)
        return InputActionTickRecord{};

    const std::size_t slot = (Newest + kHistoryCapacity - indexFromNewest) % kHistoryCapacity;
    return InputActionTickRecord{
        RingTicks[slot],
        std::span<const InputActionValue>(RingValues).subspan(slot * Actions, Actions)
    };
}
