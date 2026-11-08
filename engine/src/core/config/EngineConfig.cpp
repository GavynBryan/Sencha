#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace
{
    template <typename Config, typename Error, typename Deserialize>
    bool ReadSection(const JsonValue& root,
                     const char* sectionName,
                     Config& out,
                     std::string& error,
                     Deserialize deserialize)
    {
        const JsonValue* section = root.Find(sectionName);
        if (!section)
            return true;

        Error sectionError;
        std::optional<Config> config = deserialize(*section, &sectionError);
        if (!config)
        {
            error = std::string("engine config: ") + sectionError.Message;
            return false;
        }

        out = std::move(*config);
        return true;
    }
}

// ---------------------------------------------------------------------------
// LoadEngineConfig
//
// Reads `path` into memory, runs JsonParse, then dispatches each present
// top-level section to its config deserializer. Missing sections keep their
// default-constructed config values.
// ---------------------------------------------------------------------------

std::optional<EngineConfig> LoadEngineConfig(
    const char* path,
    EngineConfigError* error)
{
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
    if (!ReadSection<EngineAppConfig, AppConfigError>(
            *root, "app", config.App, sectionError, DeserializeAppConfig)
        || !ReadSection<EngineWindowConfig, WindowConfigError>(
            *root, "window", config.Window, sectionError, DeserializeWindowConfig)
        || !ReadSection<EngineRuntimeConfig, RuntimeConfigError>(
            *root, "runtime", config.Runtime, sectionError, DeserializeRuntimeConfig)
        || !ReadSection<EngineGraphicsConfig, GraphicsConfigError>(
            *root, "graphics", config.Graphics, sectionError, DeserializeGraphicsConfig)
        || !ReadSection<EngineDebugConfig, DebugConfigError>(
            *root, "debug", config.Debug, sectionError, DeserializeDebugConfig)
        || !ReadSection<EngineConsoleConfig, ConsoleConfigError>(
            *root, "console", config.Console, sectionError, DeserializeConsoleConfig)
        || !ReadSection<EngineAudioConfig, AudioConfigError>(
            *root, "audio", config.Audio, sectionError, DeserializeAudioConfig)
        || !ReadSection<EngineCaptionConfig, CaptionConfigError>(
            *root, "captions", config.Captions, sectionError, DeserializeCaptionConfig))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    return config;
}

void ApplyEnvironmentOverrides(EngineConfig& config)
{
    if (const char* size = std::getenv("SENCHA_WINDOW_SIZE"))
    {
        int width = 0;
        int height = 0;
        if (std::sscanf(size, "%dx%d", &width, &height) == 2 && width > 0 && height > 0)
        {
            config.Window.Width = static_cast<std::uint32_t>(width);
            config.Window.Height = static_cast<std::uint32_t>(height);
        }
    }
    const auto readBool = [](const char* name, bool& out)
    {
        if (const char* value = std::getenv(name))
            out = std::strcmp(value, "0") != 0;
    };
    readBool("SENCHA_VALIDATION", config.Graphics.EnableValidation);
    readBool("SENCHA_VALIDATE_SYNC", config.Graphics.ValidateSynchronization);
    readBool("SENCHA_VALIDATE_GPU_ASSISTED", config.Graphics.ValidateGpuAssisted);
    readBool("SENCHA_VALIDATE_BEST_PRACTICES", config.Graphics.ValidateBestPractices);
    if (const char* scratch = std::getenv("SENCHA_FRAME_SCRATCH_BYTES"))
    {
        unsigned long long bytes = 0;
        if (std::sscanf(scratch, "%llu", &bytes) == 1 && bytes > 0)
            config.Graphics.FrameScratchBytesPerFrame = bytes;
    }
    if (const char* device = std::getenv("SENCHA_DEVICE_INDEX"))
    {
        int index = -1;
        if (std::sscanf(device, "%d", &index) == 1)
            config.Graphics.DeviceIndex = index;
    }
}
