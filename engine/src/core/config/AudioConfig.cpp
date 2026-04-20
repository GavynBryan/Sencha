#include <core/config/AudioConfig.h>

#include <utility>

// ---------------------------------------------------------------------------
// DeserializeAudioConfig
//
// Expected JSON shape (the "audio" object from engine.json):
//
//   {
//     "buses": [
//       {
//         "name":        "Sfx",
//         "maxVoices":   4,
//         "volume":      1.0,
//         "muted":       false,
//         "stealPolicy": "StealOldest"   // or "Reject" (default)
//       }
//     ]
//   }
//
// The built-in "Engine" bus must not appear here; AudioService adds it
// unconditionally.
// ---------------------------------------------------------------------------

std::optional<EngineAudioConfig> DeserializeAudioConfig(
    const JsonValue& root,
    AudioConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "audio config: root must be a JSON object";
        return std::nullopt;
    }

    EngineAudioConfig config;

    const JsonValue* buses = root.Find("buses");
    if (!buses)
        return config; // no buses key -> empty config (Engine bus only)

    if (!buses->IsArray())
    {
        if (error) error->Message = "audio config: 'buses' must be an array";
        return std::nullopt;
    }

    for (const JsonValue& entry : buses->AsArray())
    {
        if (!entry.IsObject())
        {
            if (error) error->Message = "audio config: each bus entry must be an object";
            return std::nullopt;
        }

        EngineAudioBusConfig bus;

        const JsonValue* name = entry.Find("name");
        if (!name || !name->IsString() || name->AsString().empty())
        {
            if (error) error->Message = "audio config: bus entry missing 'name' string";
            return std::nullopt;
        }
        bus.Name = name->AsString();

        const JsonValue* maxVoices = entry.Find("maxVoices");
        if (maxVoices)
        {
            if (!maxVoices->IsNumber())
            {
                if (error) error->Message = "audio config: 'maxVoices' must be a number";
                return std::nullopt;
            }
            const int v = static_cast<int>(maxVoices->AsNumber());
            if (v <= 0 || v > 255)
            {
                if (error) error->Message = "audio config: 'maxVoices' must be in [1, 255]";
                return std::nullopt;
            }
            bus.MaxVoices = static_cast<uint8_t>(v);
        }

        const JsonValue* volume = entry.Find("volume");
        if (volume)
        {
            if (!volume->IsNumber())
            {
                if (error) error->Message = "audio config: 'volume' must be a number";
                return std::nullopt;
            }
            bus.Volume = static_cast<float>(volume->AsNumber());
        }

        const JsonValue* muted = entry.Find("muted");
        if (muted)
        {
            if (!muted->IsBool())
            {
                if (error) error->Message = "audio config: 'muted' must be a boolean";
                return std::nullopt;
            }
            bus.Muted = muted->AsBool();
        }

        const JsonValue* stealPolicy = entry.Find("stealPolicy");
        if (stealPolicy)
        {
            if (!stealPolicy->IsString())
            {
                if (error) error->Message = "audio config: 'stealPolicy' must be a string";
                return std::nullopt;
            }
            const std::string& policy = stealPolicy->AsString();
            if (policy == "Reject")
                bus.StealPolicy = VoiceStealPolicy::Reject;
            else if (policy == "StealOldest")
                bus.StealPolicy = VoiceStealPolicy::StealOldest;
            else
            {
                if (error)
                    error->Message = "audio config: unknown stealPolicy '" + policy + "'";
                return std::nullopt;
            }
        }

        config.Buses.push_back(std::move(bus));
    }

    return config;
}
