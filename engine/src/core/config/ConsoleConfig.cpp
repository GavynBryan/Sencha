#include <core/config/ConsoleConfig.h>

#include <core/console/ConsoleRegistry.h>

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
            error = std::string("console config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }

    bool ReadIntEither(const JsonValue& root,
                       const char* a,
                       const char* b,
                       int& out,
                       std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("console config: '") + a + "' must be a number";
            return false;
        }
        const double number = value->AsNumber();
        if (!std::isfinite(number) || number != std::floor(number))
        {
            error = std::string("console config: '") + a + "' must be an integer";
            return false;
        }
        out = static_cast<int>(number);
        return true;
    }

    std::string ScalarToString(const JsonValue& value)
    {
        if (value.IsBool())
            return value.AsBool() ? "true" : "false";
        if (value.IsNumber())
        {
            std::string text = std::to_string(value.AsNumber());
            while (text.size() > 1 && text.back() == '0')
                text.pop_back();
            if (!text.empty() && text.back() == '.')
                text.pop_back();
            return text;
        }
        if (value.IsString())
            return value.AsString();
        return {};
    }

    bool FlattenCVars(const JsonValue& value,
                      std::string prefix,
                      std::vector<EngineConsoleCVarAssignment>& out,
                      std::string& error)
    {
        if (!value.IsObject())
        {
            error = "console config: 'cvars' must be an object";
            return false;
        }

        for (const auto& [key, child] : value.AsObject())
        {
            const std::string name = prefix.empty() ? key : prefix + "." + key;
            const std::string canonical = CanonicalConsoleName(name);
            if (child.IsObject())
            {
                if (!FlattenCVars(child, canonical, out, error))
                    return false;
                continue;
            }
            if (!child.IsBool() && !child.IsNumber() && !child.IsString())
            {
                error = "console config: cvar '" + canonical + "' must be a scalar";
                return false;
            }
            if (!IsValidConsoleName(canonical))
            {
                error = "console config: invalid cvar name '" + canonical + "'";
                return false;
            }
            out.push_back({ canonical, ScalarToString(child) });
        }
        return true;
    }

    bool ReadExec(const JsonValue& root,
                  EngineConsoleConfig& config,
                  std::string& error)
    {
        const JsonValue* exec = root.Find("exec");
        if (!exec)
            return true;
        if (!exec->IsArray())
        {
            error = "console config: 'exec' must be an array of script paths";
            return false;
        }
        for (const JsonValue& entry : exec->AsArray())
        {
            if (!entry.IsString())
            {
                error = "console config: 'exec' entries must be strings";
                return false;
            }
            config.ExecScripts.push_back(entry.AsString());
        }
        return true;
    }
}

std::optional<EngineConsoleConfig> DeserializeConsoleConfig(
    const JsonValue& root,
    ConsoleConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "console config: root must be a JSON object";
        return std::nullopt;
    }

    EngineConsoleConfig config;
    std::string sectionError;
    if (!ReadBoolEither(root, "uiEnabled", "ui_enabled", config.UiEnabled, sectionError)
        || !ReadBoolEither(root, "openOnStart", "open_on_start", config.OpenOnStart, sectionError)
        || !ReadIntEither(root, "historyCapacity", "history_capacity",
                          config.HistoryCapacity, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    if (config.HistoryCapacity < 1)
    {
        if (error) error->Message = "console config: 'historyCapacity' must be at least 1";
        return std::nullopt;
    }

    if (const JsonValue* cvars = root.Find("cvars"))
    {
        if (!FlattenCVars(*cvars, {}, config.CVars, sectionError))
        {
            if (error) error->Message = sectionError;
            return std::nullopt;
        }
    }

    if (!ReadExec(root, config, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    return config;
}
