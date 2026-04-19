#include <core/config/AppConfig.h>

std::optional<EngineAppConfig> DeserializeAppConfig(
    const JsonValue& root,
    AppConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "app config: root must be a JSON object";
        return std::nullopt;
    }

    EngineAppConfig config;

    const JsonValue* name = root.Find("name");
    if (name)
    {
        if (!name->IsString())
        {
            if (error) error->Message = "app config: 'name' must be a string";
            return std::nullopt;
        }
        config.Name = name->AsString();
    }

    return config;
}
