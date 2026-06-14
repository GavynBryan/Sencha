#include <core/config/CaptionConfig.h>

#include <utility>

// ---------------------------------------------------------------------------
// DeserializeCaptionConfig
//
// Expected JSON shape (the "captions" object from engine.json):
//
//   {
//     "channels": [
//       {
//         "name":            "World",
//         "gateOnSettings":  true,    // default true
//         "maxVisibleLines": 3,       // default 3, 0 = unlimited
//         "mergeDuplicates": true     // default true
//       }
//     ]
//   }
//
// An empty or missing channel list means CaptionRuntime installs the engine
// default channel set.
// ---------------------------------------------------------------------------

std::optional<EngineCaptionConfig> DeserializeCaptionConfig(
    const JsonValue& root,
    CaptionConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "caption config: root must be a JSON object";
        return std::nullopt;
    }

    EngineCaptionConfig config;

    const JsonValue* channels = root.Find("channels");
    if (!channels)
        return config; // no channels key -> engine default channel set

    if (!channels->IsArray())
    {
        if (error) error->Message = "caption config: 'channels' must be an array";
        return std::nullopt;
    }

    for (const JsonValue& entry : channels->AsArray())
    {
        if (!entry.IsObject())
        {
            if (error) error->Message = "caption config: each channel entry must be an object";
            return std::nullopt;
        }

        CaptionChannelConfig channel;

        const JsonValue* name = entry.Find("name");
        if (!name || !name->IsString() || name->AsString().empty())
        {
            if (error) error->Message = "caption config: channel entry missing 'name' string";
            return std::nullopt;
        }
        channel.Name = name->AsString();

        const JsonValue* gate = entry.Find("gateOnSettings");
        if (gate)
        {
            if (!gate->IsBool())
            {
                if (error) error->Message = "caption config: 'gateOnSettings' must be a boolean";
                return std::nullopt;
            }
            channel.GateOnSettings = gate->AsBool();
        }

        const JsonValue* maxLines = entry.Find("maxVisibleLines");
        if (maxLines)
        {
            if (!maxLines->IsNumber())
            {
                if (error) error->Message = "caption config: 'maxVisibleLines' must be a number";
                return std::nullopt;
            }
            const int v = static_cast<int>(maxLines->AsNumber());
            if (v < 0 || v > 255)
            {
                if (error) error->Message = "caption config: 'maxVisibleLines' must be in [0, 255]";
                return std::nullopt;
            }
            channel.MaxVisibleLines = static_cast<uint8_t>(v);
        }

        const JsonValue* merge = entry.Find("mergeDuplicates");
        if (merge)
        {
            if (!merge->IsBool())
            {
                if (error) error->Message = "caption config: 'mergeDuplicates' must be a boolean";
                return std::nullopt;
            }
            channel.MergeDuplicates = merge->AsBool();
        }

        config.Channels.push_back(std::move(channel));
    }

    return config;
}
