#pragma once

#include <core/config/EngineConfig.h>
#include <core/service/ServiceHost.h>

class SdlWindow;
class SdlWindowService;

class VulkanBootstrap
{
public:
    static bool Install(ServiceHost& services,
                        const EngineConfig& config,
                        SdlWindow& window,
                        SdlWindowService& windows);
    static bool IsServiceChainValid(ServiceHost& services);
};
