#include <effects/EffectDefinitionData.h>

#include <attributes/AttributeRegistry.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>

namespace
{
    DataFieldSchema Choice(std::string key, std::string displayName, std::string summary,
                           std::vector<DataEnumChoice> choices)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Enum, std::move(key), std::move(displayName),
                                              std::move(summary));
        field.Default = choices.front().Value;
        field.Required = false;
        field.EnumChoices = std::move(choices);
        return field;
    }

    DataFieldSchema Seconds(std::string key, std::string displayName, std::string summary)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(displayName),
                                              std::move(summary));
        field.Default = 0.0;
        field.Required = false;
        field.Numeric.Minimum = 0.0;
        field.Units = "s";
        return field;
    }

    DataSchema MakeSchema()
    {
        DataFieldSchema modifier = MakeDataField(DataFieldKind::Record, "modifier", "Modifier");
        modifier.Children.push_back(MakeDataField(DataFieldKind::StableKey, "key", "Key"));
        modifier.Children.push_back(MakeDataField(DataFieldKind::String, "attribute", "Attribute",
                                                  "The attribute this changes, by name."));
        modifier.Children.push_back(Choice("op", "Operation", "How the magnitude combines with the value.",
                                           { { "add", "Add", "" }, { "multiply", "Multiply", "" },
                                             { "override", "Override", "" } }));
        DataFieldSchema magnitude = MakeDataField(DataFieldKind::Float, "magnitude", "Magnitude");
        magnitude.Default = 0.0;
        modifier.Children.push_back(std::move(magnitude));

        DataFieldSchema modifiers = MakeDataField(DataFieldKind::Array, "modifiers", "Modifiers");
        modifiers.Required = false;
        modifiers.Editor.Widget = "cards";
        modifiers.Editor.TitleKey = "attribute";
        modifiers.Children.push_back(std::move(modifier));

        DataFieldSchema tags = MakeDataField(DataFieldKind::Array, "granted_tags", "Granted tags",
                                             "Held by the target while the effect lasts.");
        tags.Required = false;
        tags.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));

        DataSchema schema;
        schema.TypeName = std::string(kEffectDefinitionType);
        schema.DisplayName = "Effect";
        schema.Description = "A change to attributes and tags: once, for a while, or until removed.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(Choice("duration", "Duration", "How long the effect lasts.",
                                              { { "instant", "Instant", "Changes Base once and is gone." },
                                                { "duration", "Duration", "Lasts for Seconds." },
                                                { "infinite", "Infinite", "Lasts until removed." } }));
        schema.Root.Children.push_back(Seconds("seconds", "Seconds", "How long a Duration effect lasts."));
        schema.Root.Children.push_back(
            Seconds("period", "Period", "When above zero, modifiers apply to Base every period instead of folding."));
        schema.Root.Children.push_back(std::move(modifiers));
        schema.Root.Children.push_back(std::move(tags));
        return schema;
    }

    float NumberOr(const JsonValue& record, std::string_view key, float fallback)
    {
        const JsonValue* value = record.Find(key);
        return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : fallback;
    }

    std::string_view TextOr(const JsonValue& record, std::string_view key, std::string_view fallback)
    {
        const JsonValue* value = record.Find(key);
        return value != nullptr && value->IsString() ? std::string_view(value->AsString()) : fallback;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto value = std::make_shared<EffectDeclaration>();
        const std::string_view duration = TextOr(data, "duration", "instant");
        value->Duration = duration == "duration" ? EffectDuration::Duration
                        : duration == "infinite" ? EffectDuration::Infinite
                                                 : EffectDuration::Instant;
        value->Seconds = NumberOr(data, "seconds", 0.0f);
        value->Period = NumberOr(data, "period", 0.0f);
        if (value->Duration == EffectDuration::Duration && value->Seconds <= 0.0f)
            return { {}, {}, "$.data.seconds A Duration effect lasts for more than zero seconds." };

        if (const JsonValue* modifiers = data.Find("modifiers"); modifiers != nullptr && modifiers->IsArray())
        {
            for (std::size_t i = 0; i < modifiers->AsArray().size(); ++i)
            {
                const JsonValue& entry = modifiers->AsArray()[i];
                EffectDeclaration::Modifier modifier;
                modifier.Attribute = std::string(TextOr(entry, "attribute", ""));
                if (modifier.Attribute.empty())
                    return { {}, {}, std::format("$.data.modifiers[{}].attribute A modifier names an attribute.", i) };
                const std::string_view op = TextOr(entry, "op", "add");
                modifier.Op = op == "multiply" ? ModifierOp::Multiply
                            : op == "override" ? ModifierOp::Override
                                               : ModifierOp::Add;
                modifier.Magnitude = NumberOr(entry, "magnitude", 0.0f);
                value->Modifiers.push_back(std::move(modifier));
            }
        }

        if (const JsonValue* tags = data.Find("granted_tags"); tags != nullptr && tags->IsArray())
        {
            for (std::size_t i = 0; i < tags->AsArray().size(); ++i)
            {
                const JsonValue& tag = tags->AsArray()[i];
                if (!tag.IsString() || tag.AsString().empty())
                    return { {}, {}, std::format("$.data.granted_tags[{}] A granted tag is a name.", i) };
                if (std::ranges::find(value->GrantedTags, tag.AsString()) != value->GrantedTags.end())
                    return { {}, {}, std::format("$.data.granted_tags[{}] '{}' is granted twice.", i, tag.AsString()) };
                value->GrantedTags.push_back(tag.AsString());
            }
        }
        if (value->GrantedTags.size() > kEffectGrantedTagCapacity)
            return { {}, {}, std::format("$.data.granted_tags An effect grants at most {} tags.",
                                         kEffectGrantedTagCapacity) };
        if (value->Duration == EffectDuration::Instant && (!value->GrantedTags.empty() || value->Period > 0.0f))
            return { {}, {}, "$.data.duration An Instant effect has no lifetime to grant tags or tick a period in." };

        return { std::move(value), {}, {} };
    }
}

void RegisterEffectDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kEffectDefinitionType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kEffectDefinitionType);
}

bool ResolveEffectDeclaration(const EffectDeclaration& declaration,
                              const AttributeRegistry& attributes,
                              const GameplayTagRegistry& tags,
                              EffectDefinition& out,
                              std::vector<std::string>& errors)
{
    const std::size_t before = errors.size();
    out = EffectDefinition{};
    out.Duration = declaration.Duration;
    out.DurationSeconds = declaration.Seconds;
    out.Period = declaration.Period;
    for (const EffectDeclaration::Modifier& modifier : declaration.Modifiers)
    {
        const AttributeId attribute = attributes.FindAttribute(modifier.Attribute);
        if (!attribute.IsValid())
        {
            errors.push_back(std::format("no attribute '{}'", modifier.Attribute));
            continue;
        }
        out.Modifiers.push_back({ attribute, modifier.Op, modifier.Magnitude });
    }
    for (const std::string& name : declaration.GrantedTags)
    {
        const GameplayTagId tag = tags.FindTag(name);
        if (!tag.IsValid())
        {
            errors.push_back(std::format("no gameplay tag '{}'", name));
            continue;
        }
        out.GrantedTags.push_back(tag);
    }
    return errors.size() == before;
}
