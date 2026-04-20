#include <core/config/WindowConfig.h>

#include <limits>

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
                    std::string& error)
    {
        const JsonValue* value = root.Find(key);
        if (!value)
            return true;
        if (!value->IsString())
        {
            error = std::string("window config: '") + key + "' must be a string";
            return false;
        }
        out = value->AsString();
        return true;
    }

    bool ReadStringEither(const JsonValue& root,
                          const char* a,
                          const char* b,
                          std::string& out,
                          std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsString())
        {
            error = std::string("window config: '") + a + "' must be a string";
            return false;
        }
        out = value->AsString();
        return true;
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
            error = std::string("window config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
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
            error = std::string("window config: '") + a + "' must be a number";
            return false;
        }

        const double number = value->AsNumber();
        if (number < static_cast<double>(minValue)
            || number > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        {
            error = std::string("window config: '") + a + "' must be an unsigned integer";
            return false;
        }

        const uint32_t converted = static_cast<uint32_t>(number);
        if (static_cast<double>(converted) != number)
        {
            error = std::string("window config: '") + a + "' must be an unsigned integer";
            return false;
        }

        out = converted;
        return true;
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
}

std::optional<EngineWindowConfig> DeserializeWindowConfig(
    const JsonValue& root,
    WindowConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "window config: root must be a JSON object";
        return std::nullopt;
    }

    EngineWindowConfig config;
    std::string sectionError;

    if (!ReadString(root, "title", config.Title, sectionError)
        || !ReadU32Either(root, "width", "width", config.Width, sectionError, 1)
        || !ReadU32Either(root, "height", "height", config.Height, sectionError, 1)
        || !ReadBoolEither(root, "resizable", "resizable", config.Resizable, sectionError)
        || !ReadBoolEither(root, "visible", "visible", config.Visible, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    std::string mode;
    if (!ReadString(root, "mode", mode, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }
    if (!mode.empty() && !ReadWindowMode(mode, config.Mode))
    {
        if (error) error->Message = "window config: unknown mode '" + mode + "'";
        return std::nullopt;
    }

    std::string graphicsApi;
    if (!ReadStringEither(root, "graphicsApi", "graphics_api", graphicsApi, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }
    if (!graphicsApi.empty() && !ReadGraphicsApi(graphicsApi, config.GraphicsApi))
    {
        if (error) error->Message = "window config: unknown graphicsApi '" + graphicsApi + "'";
        return std::nullopt;
    }

    return config;
}
