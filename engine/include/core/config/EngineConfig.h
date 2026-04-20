#pragma once

#include <core/config/AppConfig.h>
#include <core/config/AudioConfig.h>
#include <core/config/DebugConfig.h>
#include <core/config/GraphicsConfig.h>
#include <core/config/RuntimeConfig.h>
#include <core/config/WindowConfig.h>

#include <optional>
#include <string>

//=============================================================================
// EngineConfig
//
// Aggregates all launch/runtime subsystem configs that are loaded from
// engine.json at startup. Not an IService -- callers hold an EngineConfig
// value directly and pass the relevant sub-config to each service at
// construction time:
//
//   EngineConfig cfg = LoadEngineConfig("engine.json").value_or({});
//   AudioService audio(logging, cfg.Audio);
//
// Adding a new subsystem config:
//   1. Define the config struct and deserializer in the subsystem's config
//      header (see AudioConfig.h).
//   2. Add a field here.
//   3. Dispatch to the deserializer from EngineConfig.cpp.
//=============================================================================
struct EngineConfig
{
    EngineAppConfig App;
    EngineWindowConfig Window;
    EngineRuntimeConfig Runtime;
    EngineGraphicsConfig Graphics;
    EngineDebugConfig Debug;
    EngineAudioConfig Audio;
};

//=============================================================================
// EngineConfigError
//=============================================================================
struct EngineConfigError
{
    std::string Message;
};

// Load and deserialize an EngineConfig from a JSON file on disk.
// Returns std::nullopt on file I/O failure or parse error and populates
// *error when provided.
std::optional<EngineConfig> LoadEngineConfig(
    const char* path,
    EngineConfigError* error = nullptr);
