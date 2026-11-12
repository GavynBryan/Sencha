#include <render/PresentationSettings.h>

#include <memory>
#include <utility>
#include <vector>

namespace
{
DataFieldSchema Optional(DataFieldSchema field)
{
    field.Required = false;
    return field;
}

DataFieldSchema Amount(std::string key, std::string display, std::string summary)
{
    DataFieldSchema field = Optional(MakeDataField(DataFieldKind::Float, std::move(key), std::move(display),
                                                   std::move(summary)));
    field.Numeric.Minimum = 0.0;
    field.Numeric.Step = 0.05;
    return field;
}

DataSchema MakeSchema()
{
    DataFieldSchema bloom = Optional(MakeDataField(DataFieldKind::Record, "bloom", "Bloom",
                                                   "Light above the threshold spreads into a glow."));
    bloom.Children = {
        Optional(MakeDataField(DataFieldKind::Bool, "enabled", "Enabled", "Whether the view blooms at all.")),
        Amount("threshold", "Threshold", "Light above this blooms."),
        Amount("intensity", "Intensity", "How strongly the glow is added back."),
        Amount("spread", "Spread", "Spacing of the blur's taps; past 1, thin lines show gaps."),
    };

    DataSchema schema;
    schema.TypeName = std::string(kPresentationSettingsType);
    schema.DisplayName = "Presentation settings";
    schema.Description = "How every view of the game looks, unless a world, graph or scene says otherwise.";
    schema.Root = MakeDataField(DataFieldKind::Record, {}, "Presentation settings");
    schema.Root.Children = { std::move(bloom) };
    return schema;
}

void ReadFloat(const JsonValue& record, std::string_view key, std::optional<float>& out)
{
    if (const JsonValue* value = record.Find(key))
        out = static_cast<float>(value->AsNumber());
}

void WriteFloat(JsonValue::Object& record, std::string_view key, const std::optional<float>& value)
{
    if (value)
        record.emplace_back(std::string(key), JsonValue(*value));
}

template <typename T>
void Inherit(T& resolved, const std::optional<T>& authored)
{
    if (authored)
        resolved = *authored;
}

DataAssetCompileResult Compile(const JsonValue& data)
{
    auto layer = std::make_shared<PresentationSettingsLayer>();
    std::string error;
    if (!ReadPresentationSettingsLayer(data, *layer, error))
        return { {}, {}, std::move(error) };
    return { std::move(layer), {}, {} };
}
} // namespace

const DataSchema& PresentationSettingsSchema()
{
    static const DataSchema schema = MakeSchema();
    return schema;
}

bool ReadPresentationSettingsLayer(const JsonValue& value, PresentationSettingsLayer& out, std::string& error)
{
    out = {};
    std::vector<DataValidationError> errors;
    if (!ValidateDataAgainstSchema(value, PresentationSettingsSchema(), errors))
    {
        error = FormatDataValidationErrors(errors);
        return false;
    }
    if (const JsonValue* bloom = value.Find("bloom"))
    {
        if (const JsonValue* enabled = bloom->Find("enabled"))
            out.Bloom.Enabled = enabled->AsBool();
        ReadFloat(*bloom, "threshold", out.Bloom.Threshold);
        ReadFloat(*bloom, "intensity", out.Bloom.Intensity);
        ReadFloat(*bloom, "spread", out.Bloom.Spread);
    }
    return true;
}

JsonValue WritePresentationSettingsLayer(const PresentationSettingsLayer& layer)
{
    JsonValue::Object root;
    if (layer.Bloom != BloomSettingsLayer{})
    {
        JsonValue::Object bloom;
        if (layer.Bloom.Enabled)
            bloom.emplace_back("enabled", JsonValue(*layer.Bloom.Enabled));
        WriteFloat(bloom, "threshold", layer.Bloom.Threshold);
        WriteFloat(bloom, "intensity", layer.Bloom.Intensity);
        WriteFloat(bloom, "spread", layer.Bloom.Spread);
        root.emplace_back("bloom", JsonValue(std::move(bloom)));
    }
    return JsonValue(std::move(root));
}

void ApplyPresentationSettingsLayer(PresentationSettings& settings, const PresentationSettingsLayer& layer)
{
    Inherit(settings.Bloom.Enabled, layer.Bloom.Enabled);
    Inherit(settings.Bloom.Threshold, layer.Bloom.Threshold);
    Inherit(settings.Bloom.Intensity, layer.Bloom.Intensity);
    Inherit(settings.Bloom.Spread, layer.Bloom.Spread);
}

PresentationSettingsLayer AsPresentationLayer(const PresentationSettings& settings)
{
    PresentationSettingsLayer layer;
    layer.Bloom = { .Enabled = settings.Bloom.Enabled,
                    .Threshold = settings.Bloom.Threshold,
                    .Intensity = settings.Bloom.Intensity,
                    .Spread = settings.Bloom.Spread };
    return layer;
}

void RegisterPresentationSettingsData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kPresentationSettingsType), kPresentationSettingsVersion, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kPresentationSettingsType);
}
