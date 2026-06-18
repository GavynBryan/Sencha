#pragma once

#include <core/config/EngineConfig.h>
#include <platform/SdlVideoService.h>
#include <platform/SdlWindowService.h>

class LoggingProvider;
class SdlWindow;

//=============================================================================
// PlatformServices
//
// The SDL platform services, owned as a group in dependency order: the window
// service depends on the video service, so it is declared second and torn down
// first. Present only when the engine runs windowed.
//
// Non-movable (its members own SDL resources), so it lives behind a
// unique_ptr on the Engine and is constructed in place.
//=============================================================================
struct PlatformServices
{
    SdlVideoService Video;
    SdlWindowService Windows;

    explicit PlatformServices(LoggingProvider& logging)
        : Video(logging)
        , Windows(logging, Video)
    {
    }

    PlatformServices(const PlatformServices&) = delete;
    PlatformServices& operator=(const PlatformServices&) = delete;

    // Creates the engine's primary window. Returns nullptr on failure.
    SdlWindow* CreatePrimaryWindow(const EngineWindowConfig& config);
};
