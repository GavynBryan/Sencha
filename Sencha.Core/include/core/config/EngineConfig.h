#pragma once

#include <audio/AudioConfig.h>

#include <optional>
#include <string>

//=============================================================================
// EngineConfig
//
// Aggregates all subsystem configs that are loaded from engine.json at
// startup. Not an IService -- callers hold an EngineConfig value directly
// and pass the relevant sub-config to each service at construction time:
//
//   EngineConfig cfg = LoadEngineConfig("engine.json").value_or({});
//   AudioService audio(logging, cfg.Audio);
//
// Adding a new subsystem config:
//   1. Define the config struct in the subsystem's own header (e.g.
//      Sencha.2D/include/render/RenderConfig.h).
//   2. Add a field here.
//   3. Deserialize it in EngineConfig.cpp next to the audio section.
//=============================================================================
struct EngineConfig
{
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
