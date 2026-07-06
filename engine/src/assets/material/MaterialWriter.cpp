#include <assets/material/MaterialWriter.h>

#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <format>
#include <fstream>

namespace
{
    JsonValue FactorArray(const Vec4& value, std::size_t components)
    {
        const float channels[4] = { value.X, value.Y, value.Z, value.W };
        JsonValue::Array items;
        items.reserve(components);
        for (std::size_t i = 0; i < components; ++i)
            items.push_back(JsonValue(static_cast<double>(channels[i])));
        return JsonValue(std::move(items));
    }

    bool SameVec4(const Vec4& a, const Vec4& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    }

    const char* AlphaModeName(MaterialAlphaMode mode)
    {
        switch (mode)
        {
        case MaterialAlphaMode::Mask:  return "mask";
        case MaterialAlphaMode::Blend: return "blend";
        case MaterialAlphaMode::Opaque:
        default:                       return "opaque";
        }
    }
}

JsonValue WriteMaterialJson(const MaterialDescription& description)
{
    const MaterialDescription defaults;
    JsonValue::Object root;
    root.emplace_back("version", JsonValue(static_cast<int>(kSmatVersion)));

    if (!SameVec4(description.BaseColorFactor, defaults.BaseColorFactor))
        root.emplace_back("base_color_factor", FactorArray(description.BaseColorFactor, 4));
    if (!description.BaseColorTexture.Path.empty())
        root.emplace_back("base_color_texture", JsonValue(description.BaseColorTexture.Path));

    if (!description.NormalTexture.Path.empty())
        root.emplace_back("normal_texture", JsonValue(description.NormalTexture.Path));
    if (description.NormalScale != defaults.NormalScale)
        root.emplace_back("normal_scale", JsonValue(static_cast<double>(description.NormalScale)));

    if (!description.OrmTexture.Path.empty())
        root.emplace_back("orm_texture", JsonValue(description.OrmTexture.Path));
    if (description.RoughnessFactor != defaults.RoughnessFactor)
        root.emplace_back("roughness_factor", JsonValue(static_cast<double>(description.RoughnessFactor)));
    if (description.MetallicFactor != defaults.MetallicFactor)
        root.emplace_back("metallic_factor", JsonValue(static_cast<double>(description.MetallicFactor)));

    if (!SameVec4(description.EmissiveFactor, defaults.EmissiveFactor))
        root.emplace_back("emissive_factor", FactorArray(description.EmissiveFactor, 3));
    if (!description.EmissiveTexture.Path.empty())
        root.emplace_back("emissive_texture", JsonValue(description.EmissiveTexture.Path));

    if (description.AlphaMode != defaults.AlphaMode)
        root.emplace_back("alpha_mode", JsonValue(AlphaModeName(description.AlphaMode)));
    if (description.AlphaCutoff != defaults.AlphaCutoff)
        root.emplace_back("alpha_cutoff", JsonValue(static_cast<double>(description.AlphaCutoff)));

    return JsonValue(std::move(root));
}

bool SaveMaterialFile(std::string_view path,
                      const MaterialDescription& description,
                      std::string* error)
{
    std::ofstream file{ std::string(path), std::ios::trunc };
    if (!file.is_open())
    {
        if (error != nullptr)
            *error = std::format("could not write material file '{}'", path);
        return false;
    }

    file << JsonStringify(WriteMaterialJson(description), /*pretty*/ true);
    if (!file.good())
    {
        if (error != nullptr)
            *error = std::format("write failed for '{}'", path);
        return false;
    }
    return true;
}
