#include <anim/AnimBlendOverrides.h>

#include "AnimSchemaFields.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>
#include <memory>

namespace
{
    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::Field;
        using AnimSchema::Record;
        DataFieldSchema entry = Record({}, "Override", {},
            {
                Field("from", DataFieldKind::GameplayTag, "From", "The behavior the layer leaves."),
                Field("to", DataFieldKind::GameplayTag, "To", "The behavior it enters."),
                AnimBlendPolicySchema("blend", "Blend", "Replaces the destination's blend for this change."),
            });
        DataFieldSchema overrides =
            ArrayOf("overrides", "Overrides", "One per pair of behaviors.", std::move(entry), true);
        overrides.Editor.Widget = "cards";
        overrides.Editor.TitleKey = "to";

        DataSchema schema;
        schema.TypeName = std::string(kAnimBlendOverridesType);
        schema.DisplayName = "Animation blend overrides";
        schema.Description = "Blend policies for specific pairs of behaviors, capped per rig.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(overrides));
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto overrides = std::make_shared<AnimBlendOverrides>();
        GameplayTagRegistry tagSyntax;
        const JsonValue::Array& entries = data.Find("overrides")->AsArray();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const std::string at = std::format("$.data.overrides[{}]", i);
            AnimBlendOverrideDecl decl;
            for (const auto& [key, out] : { std::pair{ "from", &decl.From }, std::pair{ "to", &decl.To } })
            {
                const JsonValue* value = entries[i].Find(key);
                *out = value != nullptr && value->IsString() ? value->AsString() : std::string();
                GameplayTagError error;
                if (!tagSyntax.RegisterTag(*out, &error))
                {
                    result.Error = std::format("{}.{} {}", at, key, error.Message);
                    return result;
                }
            }
            if (decl.From == decl.To)
            {
                result.Error = at + ".to A behavior does not change to itself, so there is nothing to blend.";
                return result;
            }
            for (const AnimBlendOverrideDecl& other : overrides->Overrides)
                if (other.From == decl.From && other.To == decl.To)
                {
                    result.Error = std::format("{} '{}' to '{}' is overridden twice here.", at, decl.From, decl.To);
                    return result;
                }
            if (!ReadAnimBlendPolicy(entries[i].Find("blend"), at + ".blend", decl.Blend, result.Error))
                return result;
            overrides->Overrides.push_back(std::move(decl));
        }
        result.Value = std::move(overrides);
        return result;
    }
}

void RegisterAnimBlendOverrides(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimBlendOverridesType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimBlendOverridesType);
}
