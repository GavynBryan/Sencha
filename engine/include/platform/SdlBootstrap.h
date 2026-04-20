#pragma once

#include <core/config/EngineConfig.h>

class LoggingProvider;
class SdlWindow;
class ServiceHost;

class SdlBootstrap
{
public:
    static SdlWindow* Install(ServiceHost& services,
                              const EngineConfig& config,
                              LoggingProvider& logging);
};
