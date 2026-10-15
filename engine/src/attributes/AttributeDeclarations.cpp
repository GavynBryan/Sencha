#include <attributes/AttributeDeclarations.h>

#include <attributes/AttributeRegistry.h>

#include <format>
#include <memory>

namespace
{
    DataFieldSchema OptionalNumber(std::string key, std::string displayName, std::string summary)
    {
        DataFieldSchema value = MakeDataField(DataFieldKind::Float, key, displayName, summary);
        DataFieldSchema optional = MakeDataField(DataFieldKind::Optional, std::move(key), std::move(displayName),
                                                 std::move(summary));
        optional.Required = false;
        optional.Children.push_back(std::move(value));
        return optional;
    }

    DataSchema MakeSchema()
    {
        DataFieldSchema attribute = MakeDataField(DataFieldKind::Record, "attribute", "Attribute");
        attribute.Children.push_back(MakeDataField(DataFieldKind::StableKey, "key", "Key"));
        attribute.Children.push_back(MakeDataField(DataFieldKind::String, "name", "Name",
                                                   "What effects and abilities call it, e.g. Ammo."));
        attribute.Children.push_back(OptionalNumber("min", "Minimum", "Base and current never go below this."));
        attribute.Children.push_back(OptionalNumber("max", "Maximum", "Base and current never go above this."));
        DataFieldSchema initial = MakeDataField(DataFieldKind::Float, "default", "Default",
                                                "Base value an attribute set starts from.");
        initial.Default = 0.0;
        initial.Required = false;
        attribute.Children.push_back(std::move(initial));

        DataFieldSchema attributes = MakeDataField(DataFieldKind::Array, "attributes", "Attributes");
        attributes.Editor.Widget = "cards";
        attributes.Editor.TitleKey = "name";
        attributes.Children.push_back(std::move(attribute));

        DataSchema schema;
        schema.TypeName = std::string(kAttributeDeclarationsType);
        schema.DisplayName = "Attribute declarations";
        schema.Description = "Attributes the runtime registers when content loads.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(attributes));
        return schema;
    }

    float NumberOr(const JsonValue& record, std::string_view key, float fallback)
    {
        const JsonValue* value = record.Find(key);
        return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : fallback;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto value = std::make_shared<AttributeDeclarations>();
        const JsonValue* attributes = data.Find("attributes");
        if (attributes == nullptr || !attributes->IsArray())
            return { {}, {}, "$.data.attributes Declarations are a list of attributes." };
        for (std::size_t i = 0; i < attributes->AsArray().size(); ++i)
        {
            const JsonValue& entry = attributes->AsArray()[i];
            const JsonValue* name = entry.IsObject() ? entry.Find("name") : nullptr;
            if (name == nullptr || !name->IsString() || name->AsString().empty())
                return { {}, {}, std::format("$.data.attributes[{}].name An attribute needs a name.", i) };
            for (const AttributeDeclaration& earlier : value->Attributes)
                if (earlier.Name == name->AsString())
                    return { {}, {}, std::format("$.data.attributes[{}].name '{}' is declared twice.", i,
                                                 name->AsString()) };

            AttributeDeclaration declaration;
            declaration.Name = name->AsString();
            declaration.Min = NumberOr(entry, "min", declaration.Min);
            declaration.Max = NumberOr(entry, "max", declaration.Max);
            declaration.Default = NumberOr(entry, "default", 0.0f);
            if (declaration.Min > declaration.Max)
                return { {}, {}, std::format("$.data.attributes[{}].min '{}' has a minimum above its maximum.", i,
                                             declaration.Name) };
            if (declaration.Default < declaration.Min || declaration.Default > declaration.Max)
                return { {}, {}, std::format("$.data.attributes[{}].default '{}' starts outside its range.", i,
                                             declaration.Name) };
            value->Attributes.push_back(std::move(declaration));
        }
        return { std::move(value), {}, {} };
    }
}

void RegisterAttributeDeclarations(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAttributeDeclarationsType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAttributeDeclarationsType);
}

void DeclareAttributes(const AttributeDeclarations& declarations, AttributeRegistry& attributes,
                       std::vector<std::string>& errors)
{
    for (const AttributeDeclaration& declaration : declarations.Attributes)
    {
        const AttributeId existing = attributes.FindAttribute(declaration.Name);
        if (!existing.IsValid())
        {
            (void)attributes.RegisterAttribute(declaration.Name, declaration.Min, declaration.Max,
                                               declaration.Default);
            continue;
        }
        if (attributes.Min(existing) != declaration.Min || attributes.Max(existing) != declaration.Max
            || attributes.DefaultBase(existing) != declaration.Default)
        {
            errors.push_back(std::format("'{}' is already registered with a different range or default", declaration.Name));
        }
    }
}
