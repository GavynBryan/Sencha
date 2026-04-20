#include <core/config/RuntimeConfig.h>

#include <cmath>

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
            error = std::string("runtime config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }

    bool ReadDoubleEither(const JsonValue& root,
                          const char* a,
                          const char* b,
                          double& out,
                          std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("runtime config: '") + a + "' must be a number";
            return false;
        }
        out = value->AsNumber();
        return true;
    }
}

std::optional<EngineRuntimeConfig> DeserializeRuntimeConfig(
    const JsonValue& root,
    RuntimeConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "runtime config: root must be a JSON object";
        return std::nullopt;
    }

    EngineRuntimeConfig config;
    std::string sectionError;

    if (!ReadDoubleEither(root, "fixedTickRate", "fixed_tick_rate",
            config.FixedTickRate, sectionError)
        || !ReadDoubleEither(root, "targetFps", "target_fps",
            config.TargetFps, sectionError)
        || !ReadDoubleEither(root, "resizeSettleSeconds", "resize_settle_seconds",
            config.ResizeSettleSeconds, sectionError)
        || !ReadBoolEither(root, "exitOnEscape", "exit_on_escape",
            config.ExitOnEscape, sectionError)
        || !ReadBoolEither(root, "togglePauseOnF1", "toggle_pause_on_f1",
            config.TogglePauseOnF1, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    if (!std::isfinite(config.FixedTickRate) || config.FixedTickRate <= 0.0)
    {
        if (error) error->Message = "runtime config: 'fixedTickRate' must be greater than zero";
        return std::nullopt;
    }

    return config;
}
