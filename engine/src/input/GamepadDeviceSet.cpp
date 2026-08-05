#include <input/GamepadDeviceSet.h>

#include <algorithm>
#include <bit>
#include <cmath>

GamepadDeviceSet::Device* GamepadDeviceSet::Find(std::uint32_t deviceId)
{
    const auto existing = std::find_if(Devices.begin(), Devices.end(),
        [deviceId](const Device& device) { return device.Id == deviceId; });
    return existing == Devices.end() ? nullptr : &*existing;
}

bool GamepadDeviceSet::Contains(std::uint32_t deviceId) const
{
    return std::any_of(Devices.begin(), Devices.end(),
        [deviceId](const Device& device) { return device.Id == deviceId; });
}

void GamepadDeviceSet::Add(std::uint32_t deviceId, void* platformHandle)
{
    if (Contains(deviceId))
        return;

    // A device joins reporting nothing, so the fold is unchanged and there is
    // no edge to publish. Whatever it is physically holding arrives as its
    // first event.
    Devices.push_back(Device{ deviceId, platformHandle, 0, {} });
}

void* GamepadDeviceSet::Remove(std::uint32_t deviceId)
{
    const auto existing = std::find_if(Devices.begin(), Devices.end(),
        [deviceId](const Device& device) { return device.Id == deviceId; });
    if (existing == Devices.end())
        return nullptr;

    void* handle = existing->PlatformHandle;
    Devices.erase(existing);
    return handle;
}

std::vector<void*> GamepadDeviceSet::PlatformHandles() const
{
    std::vector<void*> handles;
    handles.reserve(Devices.size());
    for (const Device& device : Devices)
        handles.push_back(device.PlatformHandle);
    return handles;
}

void GamepadDeviceSet::SetButton(std::uint32_t deviceId, std::uint32_t button, bool down)
{
    Device* device = Find(deviceId);
    if (device == nullptr || button >= 32)
        return;

    const std::uint32_t mask = std::uint32_t{ 1 } << button;
    if (down)
        device->Buttons |= mask;
    else
        device->Buttons &= ~mask;
}

void GamepadDeviceSet::SetAxis(std::uint32_t deviceId, GamepadAxis axis, float value)
{
    Device* device = Find(deviceId);
    const auto index = static_cast<std::size_t>(axis);
    if (device == nullptr || index >= device->Axes.size())
        return;
    device->Axes[index] = value;
}

std::uint32_t GamepadDeviceSet::FoldButtons() const
{
    std::uint32_t held = 0;
    for (const Device& device : Devices)
        held |= device.Buttons;
    return held;
}

void GamepadDeviceSet::PublishButtons(InputFrame& frame, std::uint32_t before) const
{
    // Only the bits the fold moved. The frame owns held state and may have had
    // it cleared from under us, so each edge is guarded against what the frame
    // currently says, the same way the keyboard path guards its own.
    const std::uint32_t after = FoldButtons();
    std::uint32_t changed = before ^ after;
    while (changed != 0)
    {
        const auto bit = static_cast<std::uint32_t>(std::countr_zero(changed));
        changed &= changed - 1;

        const bool down = (after & (std::uint32_t{ 1 } << bit)) != 0;
        if (down && !frame.IsGamepadButtonDown(bit))
            frame.GamepadButtonsPressed.push_back(bit);
        else if (!down && frame.IsGamepadButtonDown(bit))
            frame.GamepadButtonsReleased.push_back(bit);
        frame.SetGamepadButtonHeld(bit, down);
    }
}

void GamepadDeviceSet::PublishAxes(InputFrame& frame) const
{
    // A stick folds as a stick rather than one axis at a time, so the two axes
    // the frame reports always come from the same hand.
    constexpr GamepadAxis kSticks[][2] = {
        { GamepadAxis::LeftX, GamepadAxis::LeftY },
        { GamepadAxis::RightX, GamepadAxis::RightY },
    };
    for (const auto& stick : kSticks)
    {
        float bestX = 0.0f;
        float bestY = 0.0f;
        float furthest = 0.0f;
        for (const Device& device : Devices)
        {
            const float x = device.Axes[static_cast<std::size_t>(stick[0])];
            const float y = device.Axes[static_cast<std::size_t>(stick[1])];
            if (const float distance = x * x + y * y; distance > furthest)
            {
                furthest = distance;
                bestX = x;
                bestY = y;
            }
        }
        frame.SetGamepadAxis(stick[0], bestX);
        frame.SetGamepadAxis(stick[1], bestY);
    }

    constexpr GamepadAxis kTriggers[] = { GamepadAxis::LeftTrigger, GamepadAxis::RightTrigger };
    for (const GamepadAxis axis : kTriggers)
    {
        float furthest = 0.0f;
        for (const Device& device : Devices)
        {
            const float value = device.Axes[static_cast<std::size_t>(axis)];
            if (std::fabs(value) > std::fabs(furthest))
                furthest = value;
        }
        frame.SetGamepadAxis(axis, furthest);
    }
}
