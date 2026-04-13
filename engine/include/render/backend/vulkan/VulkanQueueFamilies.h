#pragma once

#include <cstdint>
#include <optional>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>

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

    bool Satisfies(const VulkanQueueRequirements& requirements) const
    {
        if (requirements.Graphics && !HasGraphics()) return false;
        if (requirements.Present && !HasPresent()) return false;
        if (requirements.Compute && !HasCompute()) return false;
        if (requirements.Transfer && !HasTransfer()) return false;
        return true;
    }
};
