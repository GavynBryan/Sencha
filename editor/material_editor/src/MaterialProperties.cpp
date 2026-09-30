#include "MaterialProperties.h"

#include "EditMaterialCommand.h"
#include "MaterialDocumentSet.h"

#include "project/TextureImportStore.h"
#include "properties/SettingProperties.h"

#include <core/assets/AssetRegistry.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>

namespace
{
constexpr std::array<std::string_view, 2> kShadings = { "Standard Lit", "Unlit" };
constexpr std::array<std::string_view, 3> kAlphaModes = { "Opaque", "Mask", "Blend" };

bool Lit(const MaterialDescription& material) { return material.Shading == MaterialShading::StandardLit; }
bool Masked(const MaterialDescription& material) { return material.AlphaMode == MaterialAlphaMode::Mask; }

std::string Number(float value) { return std::format("{}", value); }

bool WriteNumber(float& field, std::string_view text, double min, double max)
{
    const std::optional<double> parsed = ParseNumber(text, min, max);
    if (parsed.has_value())
        field = static_cast<float>(*parsed);
    return parsed.has_value();
}

bool WriteToggle(bool& field, std::string_view text)
{
    const std::optional<bool> parsed = ParseToggle(text);
    if (parsed.has_value())
        field = *parsed;
    return parsed.has_value();
}

// "r, g, b" -- the colour's channels as the vector properties write them.
std::string Color(const Vec4& color)
{
    return std::format("{}, {}, {}", color.X, color.Y, color.Z);
}

bool WriteColor(Vec4& field, std::string_view text)
{
    const std::optional<std::vector<double>> parsed = ParseVector(text, 3);
    if (!parsed.has_value())
        return false;
    field = Vec4(static_cast<float>((*parsed)[0]), static_cast<float>((*parsed)[1]), static_cast<float>((*parsed)[2]),
                 field.W);
    return true;
}

bool WriteTexture(AssetRef& slot, std::string_view text)
{
    slot = text.empty() ? AssetRef{} : AssetRef{ AssetType::Texture, std::string(text) };
    return true;
}

struct MaterialField
{
    std::string_view Key;
    std::string_view Group;
    std::string_view Label;
    PropertyKind Kind;
    std::string (*Read)(const MaterialDescription&);
    bool (*Write)(MaterialDescription&, std::string_view);
    bool (*Relevant)(const MaterialDescription&) = nullptr;
    double Min = 0.0;
    double Max = -1.0;
    std::span<const std::string_view> Choices = {};
};

using M = MaterialDescription;

// The material's parameters in the order they are read, one row per field.
const std::array kFields = {
    MaterialField{ "shading", "Material", "Shading", PropertyKind::Choice,
           [](const M& m) { return std::string(kShadings[static_cast<std::size_t>(m.Shading)]); },
           [](M& m, std::string_view t) {
               const auto chosen = ParseChoice(t, kShadings);
               if (chosen.has_value())
                   m.Shading = static_cast<MaterialShading>(*chosen);
               return chosen.has_value();
           },
           nullptr, 0.0, -1.0, kShadings },
    MaterialField{ "double_sided", "Material", "Double sided", PropertyKind::Toggle,
           [](const M& m) { return std::string(m.DoubleSided ? "true" : "false"); },
           [](M& m, std::string_view t) { return WriteToggle(m.DoubleSided, t); } },
    MaterialField{ "cast_shadows", "Material", "Casts shadows", PropertyKind::Toggle,
           [](const M& m) { return std::string(m.CastShadows ? "true" : "false"); },
           [](M& m, std::string_view t) { return WriteToggle(m.CastShadows, t); } },
    MaterialField{ "receive_shadows", "Material", "Receives shadows", PropertyKind::Toggle,
           [](const M& m) { return std::string(m.ReceiveShadows ? "true" : "false"); },
           [](M& m, std::string_view t) { return WriteToggle(m.ReceiveShadows, t); } },
    MaterialField{ "base_color", "Base color", "Color", PropertyKind::Color,
           [](const M& m) { return Color(m.BaseColorFactor); },
           [](M& m, std::string_view t) { return WriteColor(m.BaseColorFactor, t); } },
    MaterialField{ "base_color_alpha", "Base color", "Opacity", PropertyKind::Number,
           [](const M& m) { return Number(m.BaseColorFactor.W); },
           [](M& m, std::string_view t) { return WriteNumber(m.BaseColorFactor.W, t, 0.0, 1.0); },
           nullptr, 0.0, 1.0 },
    MaterialField{ "base_color_texture", "Base color", "Texture", PropertyKind::Asset,
           [](const M& m) { return m.BaseColorTexture.Path; },
           [](M& m, std::string_view t) { return WriteTexture(m.BaseColorTexture, t); } },
    MaterialField{ "roughness", "Surface", "Roughness", PropertyKind::Number,
           [](const M& m) { return Number(m.RoughnessFactor); },
           [](M& m, std::string_view t) { return WriteNumber(m.RoughnessFactor, t, 0.0, 1.0); }, Lit, 0.0, 1.0 },
    MaterialField{ "metallic", "Surface", "Metallic", PropertyKind::Number,
           [](const M& m) { return Number(m.MetallicFactor); },
           [](M& m, std::string_view t) { return WriteNumber(m.MetallicFactor, t, 0.0, 1.0); }, Lit, 0.0, 1.0 },
    MaterialField{ "specular", "Surface", "Specular", PropertyKind::Number,
           [](const M& m) { return Number(m.SpecularIntensity); },
           [](M& m, std::string_view t) { return WriteNumber(m.SpecularIntensity, t, 0.0, 1.0); }, Lit, 0.0, 1.0 },
    MaterialField{ "orm_texture", "Surface", "ORM texture", PropertyKind::Asset,
           [](const M& m) { return m.OrmTexture.Path; },
           [](M& m, std::string_view t) { return WriteTexture(m.OrmTexture, t); }, Lit },
    MaterialField{ "normal_texture", "Normal", "Texture", PropertyKind::Asset,
           [](const M& m) { return m.NormalTexture.Path; },
           [](M& m, std::string_view t) { return WriteTexture(m.NormalTexture, t); }, Lit },
    MaterialField{ "normal_scale", "Normal", "Scale", PropertyKind::Number,
           [](const M& m) { return Number(m.NormalScale); },
           [](M& m, std::string_view t) { return WriteNumber(m.NormalScale, t, 0.0, 2.0); }, Lit, 0.0, 2.0 },
    MaterialField{ "emissive", "Emissive", "Color", PropertyKind::Color,
           [](const M& m) { return Color(m.EmissiveFactor); },
           [](M& m, std::string_view t) { return WriteColor(m.EmissiveFactor, t); } },
    MaterialField{ "emissive_strength", "Emissive", "Strength", PropertyKind::Number,
           [](const M& m) { return Number(m.EmissiveStrength); },
           [](M& m, std::string_view t) { return WriteNumber(m.EmissiveStrength, t, 0.0, 1000.0); } },
    MaterialField{ "emissive_texture", "Emissive", "Texture", PropertyKind::Asset,
           [](const M& m) { return m.EmissiveTexture.Path; },
           [](M& m, std::string_view t) { return WriteTexture(m.EmissiveTexture, t); } },
    MaterialField{ "alpha_mode", "Alpha", "Mode", PropertyKind::Choice,
           [](const M& m) { return std::string(kAlphaModes[static_cast<std::size_t>(m.AlphaMode)]); },
           [](M& m, std::string_view t) {
               const auto chosen = ParseChoice(t, kAlphaModes);
               if (chosen.has_value())
                   m.AlphaMode = static_cast<MaterialAlphaMode>(*chosen);
               return chosen.has_value();
           },
           nullptr, 0.0, -1.0, kAlphaModes },
    MaterialField{ "alpha_cutoff", "Alpha", "Cutoff", PropertyKind::Number,
           [](const M& m) { return Number(m.AlphaCutoff); },
           [](M& m, std::string_view t) { return WriteNumber(m.AlphaCutoff, t, 0.0, 1.0); }, Masked, 0.0, 1.0 },
};

const MaterialField* FieldOf(std::string_view key)
{
    const auto found = std::ranges::find(kFields, key, &MaterialField::Key);
    return found != kFields.end() ? &*found : nullptr;
}

} // namespace

std::vector<PropertyNode> MaterialProperties::Nodes() const
{
    const MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return {};
    const MaterialDescription& material = tab->Session.Working();
    std::vector<PropertyNode> groups;
    for (const MaterialField& field : kFields)
    {
        if (field.Relevant != nullptr && !field.Relevant(material))
            continue;
        if (groups.empty() || groups.back().Descriptor.Label != field.Group)
            groups.push_back(GroupProperty(std::string(field.Group), std::string(field.Group)));
        PropertyNode node;
        node.Key = std::string(field.Key);
        node.Descriptor.Kind = field.Kind;
        node.Descriptor.Label = std::string(field.Label);
        node.Descriptor.Min = field.Min;
        node.Descriptor.Max = field.Max;
        node.Descriptor.Step = field.Max > field.Min ? (field.Max - field.Min) / 100.0 : 0.0;
        node.Descriptor.Choices.assign(field.Choices.begin(), field.Choices.end());
        if (field.Kind == PropertyKind::Asset)
            node.Descriptor.Asset = AssetType::Texture;
        node.Value = field.Read(material);
        groups.back().Children.push_back(std::move(node));
    }
    return groups;
}

bool MaterialProperties::BeginEdit(std::string_view key)
{
    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen() || FieldOf(key) == nullptr)
        return false;
    Baseline = tab->Session.Working();
    return true;
}

PropertyEditResult MaterialProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    MaterialEditTab* tab = Tabs.Active();
    const MaterialField* field = FieldOf(key);
    if (tab == nullptr || field == nullptr || !Baseline.has_value())
        return PropertyEditResult::Gone;
    MaterialDescription edited = tab->Session.Working();
    if (!field->Write(edited, value))
        return PropertyEditResult::Refused;
    if (SameMaterialDescription(edited, tab->Session.Working()))
        return PropertyEditResult::Unchanged;
    tab->Session.SetWorking(edited);
    return PropertyEditResult::Applied;
}

PropertyEditResult MaterialProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult previewed = PreviewEdit(key, value);
    MaterialEditTab* tab = Tabs.Active();
    if (previewed == PropertyEditResult::Refused || previewed == PropertyEditResult::Gone || tab == nullptr)
    {
        CancelEdit(key);
        return previewed;
    }
    const MaterialDescription before = *Baseline;
    Baseline.reset();
    if (SameMaterialDescription(before, tab->Session.Working()))
        return PropertyEditResult::Unchanged;
    tab->Commands.Execute(std::make_unique<EditMaterialCommand>(tab->Session, before, tab->Session.Working()));
    return PropertyEditResult::Applied;
}

void MaterialProperties::CancelEdit(std::string_view)
{
    if (MaterialEditTab* tab = Tabs.Active(); tab != nullptr && Baseline.has_value())
        tab->Session.SetWorking(*Baseline);
    Baseline.reset();
}

std::vector<std::string> MaterialProperties::Candidates(std::string_view key) const
{
    const MaterialField* field = FieldOf(key);
    if (field == nullptr || field->Kind != PropertyKind::Asset)
        return {};
    std::vector<std::string> textures;
    for (const auto& [path, record] : Registry.Records())
        if (IsSourceTexture(record))
            textures.push_back(record.Path);
    std::ranges::sort(textures);
    return textures;
}
