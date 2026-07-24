#include <core/config/GraphicsConfig.h>

#include <limits>

namespace
{
    const JsonValue* FindEither(const JsonValue& root, const char* a, const char* b)
    {
        if (const JsonValue* value = root.Find(a))
            return value;
        return root.Find(b);
    }

    bool ReadBoolEither(const JsonValue& root,
                        const char* a,
                        const char* b,
                        bool& out,
                        std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsBool())
        {
            error = std::string("graphics config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }

    bool ReadI32Either(const JsonValue& root,
                       const char* a,
                       const char* b,
                       int32_t& out,
                       std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("graphics config: '") + a + "' must be a number";
            return false;
        }

        const double number = value->AsNumber();
        const int32_t converted = static_cast<int32_t>(number);
        if (number < static_cast<double>(std::numeric_limits<int32_t>::min())
            || number > static_cast<double>(std::numeric_limits<int32_t>::max())
            || static_cast<double>(converted) != number)
        {
            error = std::string("graphics config: '") + a + "' must be an integer";
            return false;
        }

        out = converted;
        return true;
    }

    bool ReadU32Either(const JsonValue& root,
                       const char* a,
                       const char* b,
                       uint32_t& out,
                       std::string& error,
                       uint32_t minValue)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("graphics config: '") + a + "' must be a number";
            return false;
        }

        const double number = value->AsNumber();
        if (number < static_cast<double>(minValue)
            || number > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        {
            error = std::string("graphics config: '") + a + "' must be an unsigned integer";
            return false;
        }

        const uint32_t converted = static_cast<uint32_t>(number);
        if (static_cast<double>(converted) != number)
        {
            error = std::string("graphics config: '") + a + "' must be an unsigned integer";
            return false;
        }

        out = converted;
        return true;
    }
}

std::optional<EngineGraphicsConfig> DeserializeGraphicsConfig(
    const JsonValue& root,
    GraphicsConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "graphics config: root must be a JSON object";
        return std::nullopt;
    }

    EngineGraphicsConfig config;
    std::string sectionError;

    if (!ReadU32Either(root, "framesInFlight", "frames_in_flight",
            config.FramesInFlight, sectionError, 1)
        || !ReadBoolEither(root, "enableValidation", "enable_validation",
            config.EnableValidation, sectionError)
        || !ReadI32Either(root, "deviceIndex", "device_index",
            config.DeviceIndex, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    return config;
}
