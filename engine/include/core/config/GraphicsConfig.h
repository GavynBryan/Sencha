#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>

struct EngineGraphicsConfig
{
    uint32_t FramesInFlight = 2;
    bool EnableValidation = true;
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
