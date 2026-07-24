#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>

struct EngineGraphicsConfig
{
    uint32_t FramesInFlight = 2;
    // Validation is a development instrument, so the default follows the
    // build type: on in debug builds so errors surface without opt-in, off in
    // optimized builds so a release or profiling run never silently pays the
    // layer cost (~4.8x on CPU render recording when the layer is installed).
    // Config overrides in either direction; on machines without the layer a
    // true value warns and continues without it.
#ifdef NDEBUG
    bool EnableValidation = false;
#else
    bool EnableValidation = true;
#endif
    // Validation layer checks beyond the core object/parameter set. The core
    // checks do not detect missing barriers or image layout races, so a run
    // that is clean without these has not been checked for the hazard class
    // that differs most between drivers. Off by default: each costs CPU time
    // in every frame, and GpuAssisted also consumes descriptor and buffer
    // resources on the device.
    bool ValidateSynchronization = false;
    bool ValidateGpuAssisted = false;
    bool ValidateBestPractices = false;
    // Per-frame-in-flight scratch budget for transient vertex/uniform uploads. The game
    // needs little; the editor raises this since it re-uploads the scene for every
    // viewport into one slice each frame.
    uint64_t FrameScratchBytesPerFrame = 1024 * 1024;
    // Adapter to use, by enumeration order. Negative scores devices normally
    // (the shipping behavior); a value selects one explicitly so the same
    // scene can be measured across the adapters in one machine.
    int32_t DeviceIndex = -1;
};

struct GraphicsConfigError
{
    std::string Message;
};

std::optional<EngineGraphicsConfig> DeserializeGraphicsConfig(
    const JsonValue& root,
    GraphicsConfigError* error = nullptr);
