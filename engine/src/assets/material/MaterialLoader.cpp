#include <assets/material/MaterialLoader.h>

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <format>
#include <fstream>
#include <sstream>

namespace
{
    bool Fail(MaterialParseError* error, std::string message)
    {
        if (error)
            error->Message = std::move(message);
        return false;
    }

    bool ReadFloat(const JsonValue& value, std::string_view key,
                   float& out, MaterialParseError* error)
    {
        if (!value.IsNumber())
            return Fail(error, std::format("'{}' must be a number", key));
        out = static_cast<float>(value.AsNumber());
        return true;
    }

    bool ReadFactor(const JsonValue& value, std::string_view key, std::size_t components,
                    Vec4& out, MaterialParseError* error)
    {
        if (!value.IsArray() || value.Size() != components)
            return Fail(error, std::format("'{}' must be an array of {} numbers", key, components));

        const JsonValue::Array& items = value.AsArray();
        float channels[4] = { out.X, out.Y, out.Z, out.W };
        for (std::size_t i = 0; i < components; ++i)
        {
            if (!items[i].IsNumber())
                return Fail(error, std::format("'{}' must be an array of {} numbers", key, components));
            channels[i] = static_cast<float>(items[i].AsNumber());
        }

        out = Vec4(channels[0], channels[1], channels[2], channels[3]);
        return true;
    }

    bool ReadTextureRef(const JsonValue& value, std::string_view key,
                        AssetRef& out, MaterialParseError* error)
    {
        if (!value.IsString())
            return Fail(error, std::format("'{}' must be an asset path string", key));
        if (!IsValidAssetPath(value.AsString()))
            return Fail(error, std::format("'{}' is not a valid asset path: '{}'", key, value.AsString()));

        out.Type = AssetType::Texture;
        out.Path = value.AsString();
        return true;
    }

    bool ReadAlphaMode(const JsonValue& value, MaterialAlphaMode& out, MaterialParseError* error)
    {
        if (!value.IsString())
            return Fail(error, "'alpha_mode' must be a string");

        const std::string& mode = value.AsString();
        if (mode == "opaque") { out = MaterialAlphaMode::Opaque; return true; }
        if (mode == "mask")   { out = MaterialAlphaMode::Mask;   return true; }
        if (mode == "blend")  { out = MaterialAlphaMode::Blend;  return true; }
        return Fail(error, std::format("unknown alpha_mode '{}' (expected opaque, mask, or blend)", mode));
    }
} // namespace

bool ParseMaterialJson(const JsonValue& root, MaterialDescription& out, MaterialParseError* error)
{
    if (!root.IsObject())
        return Fail(error, "material root must be a JSON object");

    const JsonValue* version = root.Find("version");
    if (version == nullptr || !version->IsNumber())
        return Fail(error, "missing or non-numeric 'version'");
    if (static_cast<uint32_t>(version->AsNumber()) != kSmatVersion)
        return Fail(error, std::format("unsupported material version {} (expected {})",
                                       version->AsNumber(), kSmatVersion));

    MaterialDescription desc;
    for (const auto& [key, value] : root.AsObject())
    {
        bool ok = true;
        if (key == "version")
            continue;
        else if (key == "base_color_factor")
            ok = ReadFactor(value, key, 4, desc.BaseColorFactor, error);
        else if (key == "base_color_texture")
            ok = ReadTextureRef(value, key, desc.BaseColorTexture, error);
        else if (key == "normal_texture")
            ok = ReadTextureRef(value, key, desc.NormalTexture, error);
        else if (key == "normal_scale")
            ok = ReadFloat(value, key, desc.NormalScale, error);
        else if (key == "orm_texture")
            ok = ReadTextureRef(value, key, desc.OrmTexture, error);
        else if (key == "roughness_factor")
            ok = ReadFloat(value, key, desc.RoughnessFactor, error);
        else if (key == "metallic_factor")
            ok = ReadFloat(value, key, desc.MetallicFactor, error);
        else if (key == "emissive_factor")
            ok = ReadFactor(value, key, 3, desc.EmissiveFactor, error);
        else if (key == "emissive_texture")
            ok = ReadTextureRef(value, key, desc.EmissiveTexture, error);
        else if (key == "alpha_mode")
            ok = ReadAlphaMode(value, desc.AlphaMode, error);
        else if (key == "alpha_cutoff")
            ok = ReadFloat(value, key, desc.AlphaCutoff, error);
        else
            return Fail(error, std::format("unknown material key '{}'", key));

        if (!ok)
            return false;
    }

    out = desc;
    return true;
}

bool LoadMaterialFromFile(std::string_view path, MaterialDescription& out, MaterialParseError* error)
{
    std::ifstream file{ std::string(path) };
    if (!file.is_open())
        return Fail(error, std::format("could not open material file '{}'", path));

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return Fail(error, std::format("material JSON parse error at {}: {}",
                                       parseError.Position, parseError.Message));

    return ParseMaterialJson(*root, out, error);
}
