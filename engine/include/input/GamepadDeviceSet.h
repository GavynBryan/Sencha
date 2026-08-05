#pragma once

#include <input/InputFrame.h>

#include <array>
#include <cstdint>
#include <vector>

//=============================================================================
// GamepadDeviceSet
//
// What every connected pad is reporting right now, and the fold that turns it
// into the one abstract pad an InputFrame carries: a button reads down while
// any device holds it, and each stick or trigger takes the device pushing it
// furthest.
//
// Per-device state is what makes the fold answerable. Merging events straight
// into one shared bitset cannot tell "both pads hold South" from "one does", so
// either pad's release would clear it, and a pad unplugged mid-press would
// leave its buttons held with no key-up ever coming.
//
// The frame still owns held state. Publishing touches a bit only when the fold
// across devices moves it, so a ReleaseAllHeld() from focus loss is respected
// rather than fought over on the next event.
//
// Free of any platform type: a capture adapter decodes its own events and
// drives this, which is also what lets the fold be tested without a device.
//=============================================================================

class GamepadDeviceSet
{
public:
    // `platformHandle` is opaque here -- the capture adapter that opened the
    // device owns whatever it means. Adding a device already present does
    // nothing, so a rescan cannot double-count one.
    void Add(std::uint32_t deviceId, void* platformHandle = nullptr);

    // Drops a device and hands back the handle it was added with, so the caller
    // can close it. Null when there was no such device.
    void* Remove(std::uint32_t deviceId);

    [[nodiscard]] bool Contains(std::uint32_t deviceId) const;
    [[nodiscard]] std::size_t Count() const { return Devices.size(); }
    [[nodiscard]] bool Empty() const { return Devices.empty(); }

    // Every handle in the set, for a caller closing all of them at once.
    [[nodiscard]] std::vector<void*> PlatformHandles() const;

    // State for one device. Ignored when the device is not in the set: an event
    // from a pad nothing opened describes a device this cannot fold.
    void SetButton(std::uint32_t deviceId, std::uint32_t button, bool down);
    void SetAxis(std::uint32_t deviceId, GamepadAxis axis, float value);

    [[nodiscard]] std::uint32_t FoldButtons() const;

    // Applies the difference between a fold taken before a device changed and
    // the fold now, as held state and edges on the frame.
    void PublishButtons(InputFrame& frame, std::uint32_t before) const;
    void PublishAxes(InputFrame& frame) const;

private:
    struct Device
    {
        std::uint32_t Id = 0;
        void* PlatformHandle = nullptr;
        std::uint32_t Buttons = 0;
        std::array<float, kInputGamepadAxisCount> Axes{};
    };

    [[nodiscard]] Device* Find(std::uint32_t deviceId);

    std::vector<Device> Devices;
};
