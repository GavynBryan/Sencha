#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>

#include <cstdio>
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
        || !ReadSection<EngineAudioConfig, AudioConfigError>(
            *root, "audio", config.Audio, sectionError, DeserializeAudioConfig))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    return config;
}
