#include <assets/cook/TextureImportSettings.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <format>
#include <fstream>
#include <optional>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }
}

std::string_view TextureUsageName(TextureUsage usage)
{
    switch (usage)
    {
    case TextureUsage::BaseColor:  return "base_color";
    case TextureUsage::Normal:     return "normal";
    case TextureUsage::Orm:        return "orm";
    case TextureUsage::Emissive:   return "emissive";
    case TextureUsage::LinearData: return "linear_data";
    case TextureUsage::Unknown:
    default:                       return "unknown";
    }
}

bool TextureUsageFromName(std::string_view name, TextureUsage& out)
{
    if (name == "base_color")  { out = TextureUsage::BaseColor;  return true; }
    if (name == "normal")      { out = TextureUsage::Normal;     return true; }
    if (name == "orm")         { out = TextureUsage::Orm;        return true; }
    if (name == "emissive")    { out = TextureUsage::Emissive;   return true; }
    if (name == "linear_data") { out = TextureUsage::LinearData; return true; }
    return false;
}

bool ParseTextureImportSettings(std::span<const std::byte> bytes,
                                TextureImportSettings& out,
                                std::string* error)
{
    out = TextureImportSettings{};
    if (bytes.empty())
        return true;

    JsonParseError parseError;
    const std::optional<JsonValue> json = JsonParse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &parseError);
    if (!json)
        return Fail(error, std::format("import settings JSON parse error at {}: {}",
                                       parseError.Position, parseError.Message));
    if (!json->IsObject())
        return Fail(error, "import settings must be a JSON object");

    for (const auto& [key, value] : json->AsObject())
    {
        if (key == "version")
        {
            if (!value.IsNumber() || static_cast<int>(value.AsNumber()) != 1)
                return Fail(error, "unsupported import settings version");
        }
        else if (key == "usage")
        {
            if (!value.IsString() || !TextureUsageFromName(value.AsString(), out.Usage))
                return Fail(error, std::format("unknown usage '{}'",
                                               value.IsString() ? value.AsString() : "?"));
        }
        else if (key == "filter")
        {
            if (!value.IsString())
                return Fail(error, "'filter' must be a string");
            if (value.AsString() == "linear")
                out.Filter = TextureFilter::Linear;
            else if (value.AsString() == "nearest")
                out.Filter = TextureFilter::Nearest;
            else
                return Fail(error, std::format("unknown filter '{}'", value.AsString()));
        }
        else if (key == "compress")
        {
            if (!value.IsBool())
                return Fail(error, "'compress' must be a bool");
            out.Compress = value.AsBool();
        }
        else if (key == "mips")
        {
            if (!value.IsBool())
                return Fail(error, "'mips' must be a bool");
            out.GenerateMips = value.AsBool();
        }
        else
        {
            return Fail(error, std::format("unknown import settings key '{}'", key));
        }
    }
    return true;
}

bool SaveTextureImportSettingsFile(std::string_view path,
                                   const TextureImportSettings& settings,
                                   std::string* error)
{
    JsonValue::Object root;
    root.emplace_back("version", JsonValue(1));
    if (settings.Usage != TextureUsage::Unknown)
        root.emplace_back("usage", JsonValue(std::string(TextureUsageName(settings.Usage))));
    root.emplace_back("filter", JsonValue(settings.Filter == TextureFilter::Nearest
                                              ? "nearest" : "linear"));
    root.emplace_back("compress", JsonValue(settings.Compress));
    root.emplace_back("mips", JsonValue(settings.GenerateMips));

    std::ofstream file{ std::string(path), std::ios::trunc };
    if (!file.is_open())
        return Fail(error, std::format("could not write import settings '{}'", path));
    file << JsonStringify(JsonValue(std::move(root)), /*pretty*/ true);
    if (!file.good())
        return Fail(error, std::format("write failed for '{}'", path));
    return true;
}
