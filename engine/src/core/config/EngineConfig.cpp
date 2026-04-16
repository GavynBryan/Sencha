#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <physics/2d/PhysicsDomain2D.h>

#include <cstdio>
#include <string>
#include <vector>

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

    // -- Physics2d section ----------------------------------------------------

    const JsonValue* physicsSection = root->Find("physics2d");
    if (physicsSection && physicsSection->IsObject())
    {
        const JsonValue* cellSize = physicsSection->Find("grid_cell_size");
        if (cellSize && cellSize->IsNumber())
            config.Physics2d.GridCellSize = static_cast<float>(cellSize->AsNumber());
    }

    return config;
}
