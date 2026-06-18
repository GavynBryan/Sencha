#pragma once

#include <core/config/EngineConfig.h>
#include <core/service/ServiceHost.h>

class LoggingProvider;
class SdlWindow;
class SdlWindowService;

class VulkanBootstrap
{
public:
    static bool Install(ServiceHost& services,
                        const EngineConfig& config,
                        LoggingProvider& logging,
                        SdlWindow& window,
                        SdlWindowService& windows);
    static bool IsServiceChainValid(ServiceHost& services);
};
