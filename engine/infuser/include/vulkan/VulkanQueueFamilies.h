#pragma once

#include <cstdint>
#include <optional>

//=============================================================================
// VulkanQueueFamilies
//
// Plain data: discovered queue family indices for a physical device.
// Present is only populated when a surface is available.
//=============================================================================
struct VulkanQueueFamilies
{
    std::optional<uint32_t> Graphics;
    std::optional<uint32_t> Present;
    std::optional<uint32_t> Compute;
    std::optional<uint32_t> Transfer;

    bool HasGraphics() const { return Graphics.has_value(); }
    bool HasPresent() const { return Present.has_value(); }
    bool HasCompute() const { return Compute.has_value(); }
    bool HasTransfer() const { return Transfer.has_value(); }

    bool IsComplete(bool requirePresent) const
    {
        if (!HasGraphics()) return false;
        if (requirePresent && !HasPresent()) return false;
        return true;
    }
};
