#include <movement/MovementProfileData.h>

#include <core/json/JsonValue.h>

#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view kTypeName = "movement.profile";

    DataFieldSchema FloatField(std::string key,
                               std::string display,
                               std::string summary,
                               std::string units = {},
                               std::optional<double> minimum = std::nullopt,
                               std::optional<double> maximum = std::nullopt,
                               bool required = false)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.DisplayName = std::move(display);
        field.Summary = std::move(summary);
        field.Units = std::move(units);
        field.Kind = DataFieldKind::Float;
        field.Numeric.Minimum = minimum;
        field.Numeric.Maximum = maximum;
        field.Numeric.Step = 0.05;
        field.Required = required;
        return field;
    }

    DataFieldSchema TuningPatchSchema(std::string key,
                                      std::string display,
                                      std::string summary)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.DisplayName = std::move(display);
        field.Summary = std::move(summary);
        field.Kind = DataFieldKind::Record;
        field.Required = false;
        field.Children = {
            FloatField("max_speed", "Maximum speed", "Maximum movement speed.", "m/s", 0.0),
            FloatField("acceleration", "Acceleration", "Rate used to approach desired velocity.", "1/s", 0.0),
            FloatField("friction", "Friction", "Supported movement friction coefficient.", "1/s", 0.0),
            FloatField("stop_speed", "Stop speed", "Low-speed friction control floor.", "m/s", 0.0),
            FloatField("wish_speed_cap", "Wish speed cap", "Cap applied to the acceleration wish term. Zero means uncapped.", "m/s", 0.0),
            FloatField("drag", "Drag", "Velocity drag coefficient.", "1/s", 0.0),
            FloatField("gravity_scale", "Gravity scale", "Multiplier applied to world gravity.", "multiplier", 0.0),
            FloatField("jump_speed", "Jump speed", "Up-axis speed supplied by jump execution.", "m/s", 0.0),
        };
        return field;
    }

    DataFieldSchema TagArray(std::string key, std::string display)
    {
        DataFieldSchema element;
        element.DisplayName = "Tag";
        element.Kind = DataFieldKind::GameplayTag;

        DataFieldSchema array;
        array.Key = std::move(key);
        array.DisplayName = std::move(display);
        array.Kind = DataFieldKind::Array;
        array.Required = false;
        array.Children.push_back(std::move(element));
        return array;
    }

    DataFieldSchema TagQuerySchema(std::string key,
                                   std::string display,
                                   std::string summary)
    {
        DataFieldSchema query;
        query.Key = std::move(key);
        query.DisplayName = std::move(display);
        query.Summary = std::move(summary);
        query.Kind = DataFieldKind::Record;
        query.Required = false;
        query.Children = {
            TagArray("all", "All tags"),
            TagArray("any", "Any tag"),
            TagArray("none", "Excluded tags"),
        };
        return query;
    }

    DataFieldSchema ConditionSchema()
    {
        DataFieldSchema mode;
        mode.Key = "mode";
        mode.DisplayName = "Mode";
        mode.Summary = "Match only while this locomotion mode is active.";
        mode.Kind = DataFieldKind::String;
        mode.Required = false;

        DataFieldSchema support;
        support.Key = "support";
        support.DisplayName = "Support";
        support.Summary = "Match the current physical support fact.";
        support.Kind = DataFieldKind::Enum;
        support.Required = false;
        support.Default = std::string("any");
        support.EnumChoices = {
            { "any", "Any", "Do not filter by support." },
            { "none", "None", "The character has no supporting surface." },
            { "stable", "Stable", "The character is on stable support." },
            { "steep", "Steep", "The character touches a surface too steep to support it." },
        };

        DataFieldSchema immersion = FloatField(
            "immersion_at_least", "Immersion at least",
            "Minimum estimated fraction of the character capsule inside the active volume.",
            "fraction", 0.0, 1.0);

        DataFieldSchema condition;
        condition.Key = "when";
        condition.DisplayName = "When";
        condition.Summary = "All authored conditions must match for this layer to apply.";
        condition.Kind = DataFieldKind::Record;
        condition.Required = false;
        condition.Children = {
            std::move(mode),
            std::move(support),
            std::move(immersion),
            TagQuerySchema("tags", "Gameplay tags", "Tag conditions evaluated against the character."),
        };
        return condition;
    }

    DataFieldSchema LayerSchema()
    {
        DataFieldSchema layer;
        layer.DisplayName = "Layer";
        layer.Kind = DataFieldKind::Record;
        layer.Children = {
            ConditionSchema(),
            TuningPatchSchema("set", "Set", "Replace authored coefficients."),
            TuningPatchSchema("scale", "Scale", "Multiply coefficients after set operations."),
            TuningPatchSchema("add", "Add", "Add to coefficients after set and scale operations."),
        };
        return layer;
    }

    DataSchema MakeMovementProfileSchema()
    {
        DataFieldSchema name;
        name.Key = "name";
        name.DisplayName = "Name";
        name.Summary = "Human-readable profile identity.";
        name.Kind = DataFieldKind::String;
        name.Default = std::string("movement_profile");

        DataFieldSchema layers;
        layers.Key = "layers";
        layers.DisplayName = "Layers";
        layers.Summary = "Ordered movement tuning layers. Authored order is conflict resolution.";
        layers.Kind = DataFieldKind::Array;
        layers.Children.push_back(LayerSchema());

        DataFieldSchema modeName;
        modeName.Key = "mode";
        modeName.DisplayName = "Mode";
        modeName.Summary = "Stable locomotion mode name.";
        modeName.Kind = DataFieldKind::String;

        DataFieldSchema modeLayers;
        modeLayers.Key = "layers";
        modeLayers.DisplayName = "Mode layers";
        modeLayers.Kind = DataFieldKind::Array;
        modeLayers.Children.push_back(LayerSchema());

        DataFieldSchema mode;
        mode.DisplayName = "Mode profile";
        mode.Kind = DataFieldKind::Record;
        mode.Children = {
            std::move(modeName),
            TagQuerySchema("sustain", "Sustain", "Tags required to remain in the mode."),
            std::move(modeLayers),
        };

        DataFieldSchema modes;
        modes.Key = "modes";
        modes.DisplayName = "Modes";
        modes.Summary = "Optional mode-specific tuning and sustain conditions.";
        modes.Kind = DataFieldKind::Array;
        modes.Required = false;
        modes.Children.push_back(std::move(mode));

        DataFieldSchema root;
        root.Kind = DataFieldKind::Record;
        root.Children = { std::move(name), std::move(layers), std::move(modes) };

        DataSchema schema;
        schema.TypeName = std::string(kTypeName);
        schema.DisplayName = "Movement Profile";
        schema.Description = "Ordered, hot-reloadable character movement tuning.";
        schema.Root = std::move(root);
        return schema;
    }

    std::optional<float> OptionalFloat(const JsonValue& object, std::string_view key)
    {
        const JsonValue* value = object.Find(key);
        if (value == nullptr || !value->IsNumber())
            return std::nullopt;
        return static_cast<float>(value->AsNumber());
    }

    MovementTuningPatch ParsePatch(const JsonValue* value)
    {
        MovementTuningPatch patch;
        if (value == nullptr || !value->IsObject())
            return patch;

        patch.MaxSpeed = OptionalFloat(*value, "max_speed");
        patch.Acceleration = OptionalFloat(*value, "acceleration");
        patch.Friction = OptionalFloat(*value, "friction");
        patch.StopSpeed = OptionalFloat(*value, "stop_speed");
        patch.WishSpeedCap = OptionalFloat(*value, "wish_speed_cap");
        patch.Drag = OptionalFloat(*value, "drag");
        patch.GravityScale = OptionalFloat(*value, "gravity_scale");
        patch.JumpSpeed = OptionalFloat(*value, "jump_speed");
        return patch;
    }

    void ParseTags(const JsonValue* query,
                   std::vector<std::string>& all,
                   std::vector<std::string>& any,
                   std::vector<std::string>& none)
    {
        if (query == nullptr || !query->IsObject())
            return;

        const auto read = [query](std::string_view key, std::vector<std::string>& output)
        {
            const JsonValue* values = query->Find(key);
            if (values == nullptr || !values->IsArray())
                return;
            for (const JsonValue& value : values->AsArray())
            {
                if (value.IsString())
                    output.push_back(value.AsString());
            }
        };
        read("all", all);
        read("any", any);
        read("none", none);
    }

    MovementLayerCondition ParseCondition(const JsonValue* value)
    {
        MovementLayerCondition condition;
        if (value == nullptr || !value->IsObject())
            return condition;

        if (const JsonValue* mode = value->Find("mode"); mode && mode->IsString())
            condition.Mode = mode->AsString();
        if (const JsonValue* support = value->Find("support"); support && support->IsString())
        {
            if (support->AsString() == "none") condition.Support = MovementSupportCondition::None;
            else if (support->AsString() == "stable") condition.Support = MovementSupportCondition::Stable;
            else if (support->AsString() == "steep") condition.Support = MovementSupportCondition::Steep;
        }
        condition.ImmersionAtLeast = OptionalFloat(*value, "immersion_at_least");
        ParseTags(value->Find("tags"), condition.AllTags, condition.AnyTags, condition.NoneTags);
        return condition;
    }

    bool ParseLayers(const JsonValue& value,
                     std::string_view path,
                     std::vector<MovementProfileLayer>& output,
                     std::string& error)
    {
        if (!value.IsArray())
        {
            error = std::format("{} must be an array", path);
            return false;
        }

        for (size_t index = 0; index < value.AsArray().size(); ++index)
        {
            const JsonValue& item = value.AsArray()[index];
            if (!item.IsObject())
            {
                error = std::format("{}[{}] must be an object", path, index);
                return false;
            }

            MovementProfileLayer layer;
            layer.When = ParseCondition(item.Find("when"));
            layer.Set = ParsePatch(item.Find("set"));
            layer.Scale = ParsePatch(item.Find("scale"));
            layer.Add = ParsePatch(item.Find("add"));
            layer.SourcePath = std::format("{}[{}]", path, index);
            output.push_back(std::move(layer));
        }
        return true;
    }

    DataAssetCompileResult CompileMovementProfile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        if (!data.IsObject())
        {
            result.Error = "movement profile data must be an object";
            return result;
        }

        const JsonValue* name = data.Find("name");
        const JsonValue* layers = data.Find("layers");
        if (name == nullptr || !name->IsString() || layers == nullptr)
        {
            result.Error = "movement profile requires name and layers";
            return result;
        }

        auto profile = std::make_shared<CompiledMovementProfile>();
        profile->Name = name->AsString();
        if (!ParseLayers(*layers, "$.data.layers", profile->Layers, result.Error))
            return result;

        if (const JsonValue* modes = data.Find("modes"))
        {
            if (!modes->IsArray())
            {
                result.Error = "$.data.modes must be an array";
                return result;
            }

            for (size_t index = 0; index < modes->AsArray().size(); ++index)
            {
                const JsonValue& item = modes->AsArray()[index];
                if (!item.IsObject())
                {
                    result.Error = std::format("$.data.modes[{}] must be an object", index);
                    return result;
                }
                const JsonValue* modeName = item.Find("mode");
                const JsonValue* modeLayers = item.Find("layers");
                if (modeName == nullptr || !modeName->IsString()
                    || modeLayers == nullptr)
                {
                    result.Error = std::format("$.data.modes[{}] requires mode and layers", index);
                    return result;
                }

                MovementModeProfile mode;
                mode.Mode = modeName->AsString();
                ParseTags(item.Find("sustain"), mode.SustainAllTags,
                          mode.SustainAnyTags, mode.SustainNoneTags);
                if (!ParseLayers(*modeLayers,
                                 std::format("$.data.modes[{}].layers", index),
                                 mode.Layers, result.Error))
                {
                    return result;
                }
                profile->Modes.push_back(std::move(mode));
            }
        }

        result.Value = std::move(profile);
        return result;
    }
}

void RegisterMovementProfileData(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompileMovementProfile;
    if (!types.Register(std::move(type)))
        return;

    if (!schemas.Register(MakeMovementProfileSchema()))
        (void)types.Unregister(kTypeName);
}

void UnregisterMovementProfileData(DataAssetTypeRegistry& types,
                                   DataSchemaRegistry& schemas)
{
    if (types.Unregister(kTypeName))
        (void)schemas.Unregister(kTypeName);
}
