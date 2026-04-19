#pragma once

#include <audio/AudioConfig.h>
#include <platform/WindowTypes.h>

#include <cstdint>
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
//   1. Define the config struct in the subsystem's own header (e.g.
//      Sencha.2D/include/render/RenderConfig.h).
//   2. Add a field here.
//   3. Deserialize it in EngineConfig.cpp next to the audio section.
//=============================================================================
struct EngineAppConfig
{
    std::string Name = "Sencha Application";
};

struct EngineWindowConfig
{
    std::string Title = "Sencha";
    uint32_t Width = 1280;
    uint32_t Height = 720;
    WindowMode Mode = WindowMode::Windowed;
    WindowGraphicsApi GraphicsApi = WindowGraphicsApi::Vulkan;
    bool Resizable = true;
    bool Visible = true;
};

struct EngineRuntimeConfig
{
    double FixedTickRate = 60.0;
    double TargetFps = 0.0;
    double ResizeSettleSeconds = 0.10;
    bool ExitOnEscape = false;
    bool TogglePauseOnF1 = false;
};

struct EngineGraphicsConfig
{
    uint32_t FramesInFlight = 2;
    bool EnableValidation = true;
};

struct EngineDebugConfig
{
    bool ConsoleLogging = true;
    bool DebugUi = false;
};

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
