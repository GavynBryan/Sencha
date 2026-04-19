#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    const JsonValue* FindEither(const JsonValue& root, const char* a, const char* b)
    {
        if (const JsonValue* value = root.Find(a))
            return value;
        return root.Find(b);
    }

    bool ReadString(const JsonValue& root,
                    const char* key,
                    std::string& out,
                    std::string& error,
                    const char* section)
    {
        const JsonValue* value = root.Find(key);
        if (!value)
            return true;
        if (!value->IsString())
        {
            error = std::string("engine config: '") + section + "." + key + "' must be a string";
            return false;
        }
        out = value->AsString();
        return true;
    }

    bool ReadStringEither(const JsonValue& root,
                          const char* a,
                          const char* b,
                          std::string& out,
                          std::string& error,
                          const char* section)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsString())
        {
            error = std::string("engine config: '") + section + "." + a + "' must be a string";
            return false;
        }
        out = value->AsString();
        return true;
    }

    bool ReadBoolEither(const JsonValue& root,
                        const char* a,
                        const char* b,
                        bool& out,
                        std::string& error,
                        const char* section)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsBool())
        {
            error = std::string("engine config: '") + section + "." + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }

    bool ReadDoubleEither(const JsonValue& root,
                          const char* a,
                          const char* b,
                          double& out,
                          std::string& error,
                          const char* section)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("engine config: '") + section + "." + a + "' must be a number";
            return false;
        }
        out = value->AsNumber();
        return true;
    }

    bool ReadU32Either(const JsonValue& root,
                       const char* a,
                       const char* b,
                       uint32_t& out,
                       std::string& error,
                       const char* section,
                       uint32_t minValue = 0)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("engine config: '") + section + "." + a + "' must be a number";
            return false;
        }

        const double number = value->AsNumber();
        if (number < static_cast<double>(minValue)
            || number > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        {
            error = std::string("engine config: '") + section + "." + a
                + "' must be an unsigned integer";
            return false;
        }

        const uint32_t converted = static_cast<uint32_t>(number);
        if (static_cast<double>(converted) != number)
        {
            error = std::string("engine config: '") + section + "." + a
                + "' must be an unsigned integer";
            return false;
        }

        out = converted;
        return true;
    }

    bool ReadAppConfig(const JsonValue& root, EngineConfig& config, std::string& error)
    {
        const JsonValue* section = root.Find("app");
        if (!section)
            return true;
        if (!section->IsObject())
        {
            error = "engine config: 'app' must be a JSON object";
            return false;
        }
        return ReadString(*section, "name", config.App.Name, error, "app");
    }

    bool ReadWindowMode(const std::string& value, WindowMode& mode)
    {
        if (value == "windowed" || value == "Windowed")
        {
            mode = WindowMode::Windowed;
            return true;
        }
        if (value == "fullscreen" || value == "Fullscreen")
        {
            mode = WindowMode::Fullscreen;
            return true;
        }
        if (value == "borderlessFullscreen" || value == "borderless_fullscreen"
            || value == "BorderlessFullscreen")
        {
            mode = WindowMode::BorderlessFullscreen;
            return true;
        }
        return false;
    }

    bool ReadGraphicsApi(const std::string& value, WindowGraphicsApi& api)
    {
        if (value == "none" || value == "None")
        {
            api = WindowGraphicsApi::None;
            return true;
        }
        if (value == "vulkan" || value == "Vulkan")
        {
            api = WindowGraphicsApi::Vulkan;
            return true;
        }
        return false;
    }

    bool ReadWindowConfig(const JsonValue& root, EngineConfig& config, std::string& error)
    {
        const JsonValue* section = root.Find("window");
        if (!section)
            return true;
        if (!section->IsObject())
        {
            error = "engine config: 'window' must be a JSON object";
            return false;
        }

        if (!ReadString(*section, "title", config.Window.Title, error, "window")
            || !ReadU32Either(*section, "width", "width", config.Window.Width, error, "window", 1)
            || !ReadU32Either(*section, "height", "height", config.Window.Height, error, "window", 1)
            || !ReadBoolEither(*section, "resizable", "resizable", config.Window.Resizable, error, "window")
            || !ReadBoolEither(*section, "visible", "visible", config.Window.Visible, error, "window"))
        {
            return false;
        }

        std::string mode;
        if (!ReadString(*section, "mode", mode, error, "window"))
            return false;
        if (!mode.empty() && !ReadWindowMode(mode, config.Window.Mode))
        {
            error = "engine config: unknown window.mode '" + mode + "'";
            return false;
        }

        std::string graphicsApi;
        if (!ReadStringEither(*section, "graphicsApi", "graphics_api", graphicsApi, error, "window"))
            return false;
        if (!graphicsApi.empty() && !ReadGraphicsApi(graphicsApi, config.Window.GraphicsApi))
        {
            error = "engine config: unknown window.graphicsApi '" + graphicsApi + "'";
            return false;
        }

        return true;
    }

    bool ReadRuntimeConfig(const JsonValue& root, EngineConfig& config, std::string& error)
    {
        const JsonValue* section = root.Find("runtime");
        if (!section)
            return true;
        if (!section->IsObject())
        {
            error = "engine config: 'runtime' must be a JSON object";
            return false;
        }

        if (!(ReadDoubleEither(*section, "fixedTickRate", "fixed_tick_rate",
                config.Runtime.FixedTickRate, error, "runtime")
            && ReadDoubleEither(*section, "targetFps", "target_fps",
                config.Runtime.TargetFps, error, "runtime")
            && ReadDoubleEither(*section, "resizeSettleSeconds", "resize_settle_seconds",
                config.Runtime.ResizeSettleSeconds, error, "runtime")
            && ReadBoolEither(*section, "exitOnEscape", "exit_on_escape",
                config.Runtime.ExitOnEscape, error, "runtime")
            && ReadBoolEither(*section, "togglePauseOnF1", "toggle_pause_on_f1",
                config.Runtime.TogglePauseOnF1, error, "runtime")))
        {
            return false;
        }

        if (!std::isfinite(config.Runtime.FixedTickRate) || config.Runtime.FixedTickRate <= 0.0)
        {
            error = "engine config: 'runtime.fixedTickRate' must be greater than zero";
            return false;
        }

        return true;
    }

    bool ReadGraphicsConfig(const JsonValue& root, EngineConfig& config, std::string& error)
    {
        const JsonValue* section = root.Find("graphics");
        if (!section)
            return true;
        if (!section->IsObject())
        {
            error = "engine config: 'graphics' must be a JSON object";
            return false;
        }

        return ReadU32Either(*section, "framesInFlight", "frames_in_flight",
                   config.Graphics.FramesInFlight, error, "graphics", 1)
            && ReadBoolEither(*section, "enableValidation", "enable_validation",
                   config.Graphics.EnableValidation, error, "graphics");
    }

    bool ReadDebugConfig(const JsonValue& root, EngineConfig& config, std::string& error)
    {
        const JsonValue* section = root.Find("debug");
        if (!section)
            return true;
        if (!section->IsObject())
        {
            error = "engine config: 'debug' must be a JSON object";
            return false;
        }

        return ReadBoolEither(*section, "consoleLogging", "console_logging",
                   config.Debug.ConsoleLogging, error, "debug")
            && ReadBoolEither(*section, "debugUi", "debug_ui",
                   config.Debug.DebugUi, error, "debug");
    }
}

// ---------------------------------------------------------------------------
// LoadEngineConfig
//
// Reads `path` into memory, runs JsonParse, then dispatches to each
// subsystem deserializer. Missing subsystem sections are silently treated as
// default-constructed configs -- engines without audio or other optional
// subsystems will just get zero buses, etc.
//
// Expected top-level JSON shape:
//
//   {
//     "app": { "name": "Sencha Game" },
//     "window": { "title": "Sencha", "width": 1280, "height": 720 },
//     "runtime": { "fixedTickRate": 60.0, "targetFps": 0.0 },
//     "graphics": { "framesInFlight": 2 },
//     "debug": { "consoleLogging": true, "debugUi": false },
//     "audio": {
//       "buses": [ ... ]
//     },
//     "physics2d": {
//       "grid_cell_size": 4.0
//     }
//   }
// ---------------------------------------------------------------------------

std::optional<EngineConfig> LoadEngineConfig(
    const char* path,
    EngineConfigError* error)
{
    // -- Read file into string -------------------------------------------------

    std::FILE* f = std::fopen(path, "rb");
    if (!f)
    {
        if (error) error->Message = std::string("engine config: cannot open '") + path + "'";
        return std::nullopt;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::rewind(f);

    std::string text;
    if (size > 0)
    {
        text.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(text.data(), 1, text.size(), f);
        text.resize(read);
    }
    std::fclose(f);

    // -- Parse JSON -----------------------------------------------------------

    JsonParseError parseError;
    std::optional<JsonValue> root = JsonParse(text, &parseError);
    if (!root)
    {
        if (error)
            error->Message = std::string("engine config: JSON parse error in '")
                           + path + "': " + parseError.Message;
        return std::nullopt;
    }

    if (!root->IsObject())
    {
        if (error) error->Message = "engine config: root must be a JSON object";
        return std::nullopt;
    }

    EngineConfig config;
    std::string sectionError;
    if (!ReadAppConfig(*root, config, sectionError)
        || !ReadWindowConfig(*root, config, sectionError)
        || !ReadRuntimeConfig(*root, config, sectionError)
        || !ReadGraphicsConfig(*root, config, sectionError)
        || !ReadDebugConfig(*root, config, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    // -- Audio section --------------------------------------------------------

    const JsonValue* audioSection = root->Find("audio");
    if (audioSection)
    {
        AudioConfigError audioError;
        std::optional<EngineAudioConfig> audio = DeserializeAudioConfig(*audioSection, &audioError);
        if (!audio)
        {
            if (error)
                error->Message = std::string("engine config: ") + audioError.Message;
            return std::nullopt;
        }
        config.Audio = std::move(*audio);
    }

    return config;
}
