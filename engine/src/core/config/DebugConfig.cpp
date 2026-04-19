#include <core/config/DebugConfig.h>

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
            error = std::string("debug config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }
}

std::optional<EngineDebugConfig> DeserializeDebugConfig(
    const JsonValue& root,
    DebugConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "debug config: root must be a JSON object";
        return std::nullopt;
    }

    EngineDebugConfig config;
    std::string sectionError;

    if (!ReadBoolEither(root, "consoleLogging", "console_logging",
            config.ConsoleLogging, sectionError)
        || !ReadBoolEither(root, "debugUi", "debug_ui",
            config.DebugUi, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    return config;
}
