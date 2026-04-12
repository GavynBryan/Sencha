#pragma once

#include <logging/Logger.h>
#include <vulkan/IVulkanSurfaceProvider.h>

class SdlWindow;

class SdlVulkanSurfaceProvider final : public IVulkanSurfaceProvider
{
public:
    SdlVulkanSurfaceProvider(Logger& logger, SdlWindow& window);

    [[nodiscard]] std::vector<const char*> GetRequiredInstanceExtensions() const override;
    [[nodiscard]] VulkanSurfaceResult CreateSurface(VkInstance instance) const override;

private:
    Logger& Log;
    SdlWindow& Window;
};
