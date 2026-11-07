#include "authoring/PresentationProperties.h"

#include "properties/SettingProperties.h"

#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace
{
constexpr std::string_view kGroup = "presentation";
constexpr std::array<std::string_view, 3> kToggle = { "Inherit", "On", "Off" };

const JsonValue* FieldOf(const JsonValue& layer, std::string_view record, std::string_view field)
{
    const JsonValue* members = layer.Find(record);
    return members != nullptr ? members->Find(field) : nullptr;
}

std::string Text(const JsonValue& value)
{
    return value.IsBool() ? std::string(value.AsBool() ? "On" : "Off") : std::format("{:g}", value.AsNumber());
}

std::string SourceName(const PresentationLayerEntry& layer)
{
    switch (layer.Level)
    {
    case PresentationLayerLevel::Game:  return "the game";
    case PresentationLayerLevel::World: return layer.Name.empty() ? "the world" : "world " + std::string(layer.Name);
    case PresentationLayerLevel::Graph: return layer.Name.empty() ? "its graph" : "graph " + std::string(layer.Name);
    case PresentationLayerLevel::Scene: return "the scene";
    }
    return {};
}

// Who sets a field below this layer: the highest layer that does, else the default.
std::string InheritedFrom(std::span<const PresentationLayerEntry> below, std::string_view record,
                          std::string_view field)
{
    for (auto layer = below.rbegin(); layer != below.rend(); ++layer)
        if (FieldOf(WritePresentationSettingsLayer(*layer->Settings), record, field) != nullptr)
            return SourceName(*layer);
    return "the default";
}

PropertyNode Row(const DataFieldSchema& field, std::string key, const JsonValue* authored, const JsonValue& resolved,
                 const std::string& from)
{
    const std::string inherited = std::format("{} (inherited: {} from {})", field.Summary, Text(resolved), from);
    if (field.Kind == DataFieldKind::Bool)
    {
        const std::size_t current = authored == nullptr ? 0 : authored->AsBool() ? 1 : 2;
        return ChoiceProperty(std::move(key), field.DisplayName, kToggle, current,
                              authored == nullptr ? inherited : field.Summary);
    }
    return TextProperty(std::move(key), field.DisplayName, Text(authored != nullptr ? *authored : resolved),
                        authored == nullptr ? inherited + "; type to set it here" : field.Summary + " Empty it to inherit.");
}

struct RowKey
{
    std::string_view Record;
    std::string_view Field;
};

std::optional<RowKey> ParseKey(std::string_view key)
{
    if (!key.starts_with(kGroup) || key.size() <= kGroup.size() + 1 || key[kGroup.size()] != '.')
        return std::nullopt;
    const std::string_view rest = key.substr(kGroup.size() + 1);
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;
    const RowKey row{ rest.substr(0, dot), rest.substr(dot + 1) };
    const DataFieldSchema* record = FindChild(PresentationSettingsSchema().Root, row.Record);
    if (record == nullptr || FindChild(*record, row.Field) == nullptr)
        return std::nullopt;
    return row;
}

void Erase(JsonValue::Object& object, std::string_view key)
{
    std::erase_if(object, [&](const auto& member) { return member.first == key; });
}
} // namespace

PropertyNode PresentationProperties(const PresentationSettingsLayer& layer,
                                    std::span<const PresentationLayerEntry> below)
{
    const JsonValue authored = WritePresentationSettingsLayer(layer);
    const JsonValue resolved = WritePresentationSettingsLayer(AsPresentationLayer(ResolvePresentationSettings(below)));

    const DataFieldSchema& root = PresentationSettingsSchema().Root;
    PropertyNode group = GroupProperty(std::string(kGroup), root.DisplayName);
    for (const DataFieldSchema& record : root.Children)
    {
        PropertyNode section = GroupProperty(std::format("{}.{}", kGroup, record.Key), record.DisplayName);
        section.Descriptor.Tooltip = record.Summary;
        for (const DataFieldSchema& field : record.Children)
            section.Children.push_back(Row(field, std::format("{}.{}.{}", kGroup, record.Key, field.Key),
                                           FieldOf(authored, record.Key, field.Key),
                                           *FieldOf(resolved, record.Key, field.Key),
                                           InheritedFrom(below, record.Key, field.Key)));
        group.Children.push_back(std::move(section));
    }
    return group;
}

bool IsPresentationKey(std::string_view key)
{
    return ParseKey(key).has_value();
}

std::optional<PresentationSettingsLayer> EditPresentationLayer(const PresentationSettingsLayer& layer,
                                                              std::string_view key, std::string_view value)
{
    const std::optional<RowKey> row = ParseKey(key);
    if (!row.has_value())
        return std::nullopt;
    const DataFieldSchema& field = *FindChild(*FindChild(PresentationSettingsSchema().Root, row->Record), row->Field);

    std::optional<JsonValue> written;
    if (field.Kind == DataFieldKind::Bool)
    {
        const std::optional<std::size_t> choice = ParseChoice(value, kToggle);
        if (!choice.has_value())
            return std::nullopt;
        if (*choice != 0)
            written = JsonValue(*choice == 1);
    }
    else if (!value.empty())
    {
        const std::optional<double> number = ParseNumber(value);
        if (!number.has_value())
            return std::nullopt;
        written = JsonValue(*number);
    }

    JsonValue json = WritePresentationSettingsLayer(layer);
    JsonValue::Object& root = json.AsObject();
    auto record = std::ranges::find(root, row->Record, &JsonValue::Object::value_type::first);
    if (record == root.end())
        record = root.insert(root.end(), { std::string(row->Record), JsonValue(JsonValue::Object{}) });
    JsonValue::Object& members = record->second.AsObject();
    Erase(members, row->Field);
    if (written.has_value())
        members.emplace_back(std::string(row->Field), std::move(*written));
    if (members.empty())
        Erase(root, row->Record);

    PresentationSettingsLayer edited;
    std::string error;
    if (!ReadPresentationSettingsLayer(json, edited, error))
        return std::nullopt;
    return edited;
}
