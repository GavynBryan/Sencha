#include <anim/AnimSelectorData.h>

#include "AnimSchemaFields.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>
#include <memory>

namespace
{
    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::DataRef;
        using AnimSchema::Enum;
        using AnimSchema::Field;
        using AnimSchema::Record;
        const auto optional = [](DataFieldSchema field) {
            field.Required = false;
            return field;
        };

        DataFieldSchema priority = Field("priority", DataFieldKind::Int, "Priority",
                                         "Higher wins; equal priorities keep their listed order.", false);
        DataFieldSchema holdMin = Field("hold_min_ms", DataFieldKind::Float, "Minimum hold (ms)",
                                        "A lower or equal band cannot replace this winner sooner.", false);
        holdMin.Numeric.Minimum = 0.0;
        holdMin.Units = "ms";
        DataFieldSchema cooldown = Field("cooldown_ms", DataFieldKind::Float, "Cooldown (ms)",
                                         "This rule cannot win again this soon after losing.", false);
        cooldown.Numeric.Minimum = 0.0;
        cooldown.Units = "ms";

        DataFieldSchema weight = Field("weight", DataFieldKind::Float, "Layer weight",
                                       "Weights the layer while this rule is the first weight rule to pass.", false);
        weight.Numeric.Minimum = 0.0;
        weight.Numeric.Maximum = 1.0;

        DataFieldSchema rule = Record({}, "Rule", {},
            {
                optional(Field("name", DataFieldKind::String, "Name", "A label for the debugger.")),
                std::move(priority),
                AnimPredicateSchema("enter", "Enter", "Every row must pass for the rule to win."),
                AnimPredicateSchema("stay", "Stay",
                                    "Evaluated only while the rule is winning; defaults to enter."),
                optional(Field("behavior", DataFieldKind::GameplayTag, "Behavior",
                               "The behavior the rule selects.")),
                DataRef("delegate", "Delegate", "A selector whose rules nest under this one.",
                        kAnimSelectorType),
                optional(Field("extension", DataFieldKind::String, "Extension point",
                               "A name a rig binds a selector to.")),
                std::move(weight),
                optional(Field("weight_fact", DataFieldKind::String, "Weight fact",
                               "A float fact whose value, clamped to [0, 1], weights the layer.")),
                std::move(holdMin),
                std::move(cooldown),
            });
        DataFieldSchema rules = ArrayOf("rules", "Rules", "Evaluated first match by priority.",
                                        std::move(rule), true);
        rules.Editor.Widget = "cards";
        rules.Editor.TitleKey = "name";

        DataSchema schema;
        schema.TypeName = std::string(kAnimSelectorType);
        schema.DisplayName = "Animation selector";
        schema.Description = "Rules that choose one behavior per layer from facts and requests.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(rules));
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto selector = std::make_shared<AnimSelectorData>();
        GameplayTagRegistry tagSyntax;

        const JsonValue::Array& rules = data.Find("rules")->AsArray();
        for (std::size_t i = 0; i < rules.size(); ++i)
        {
            const JsonValue& entry = rules[i];
            const std::string at = std::format("$.data.rules[{}]", i);
            AnimSelectorRuleDecl rule;
            if (const JsonValue* name = entry.Find("name"); name != nullptr && name->IsString())
                rule.Name = name->AsString();
            if (const JsonValue* priority = entry.Find("priority"); priority != nullptr && priority->IsNumber())
                rule.Priority = static_cast<std::int32_t>(priority->AsNumber());
            if (!ReadAnimPredicate(entry.Find("enter"), at + ".enter", rule.Enter, result.Error))
                return result;
            if (const JsonValue* stay = entry.Find("stay"); stay != nullptr)
            {
                rule.HasStay = true;
                if (!ReadAnimPredicate(stay, at + ".stay", rule.Stay, result.Error))
                    return result;
            }

            const auto text = [&](std::string_view key) -> const std::string* {
                const JsonValue* value = entry.Find(key);
                return value != nullptr && value->IsString() && !value->AsString().empty()
                    ? &value->AsString()
                    : nullptr;
            };
            const std::string* behavior = text("behavior");
            const std::string* delegate = text("delegate");
            const std::string* extension = text("extension");
            const std::string* weightFact = text("weight_fact");
            const JsonValue* weight = entry.Find("weight");
            const bool weighs = weightFact != nullptr || (weight != nullptr && weight->IsNumber());
            if ((behavior != nullptr) + (delegate != nullptr) + (extension != nullptr) + (weightFact != nullptr)
                    + (weight != nullptr && weight->IsNumber())
                != 1)
            {
                result.Error = at + " A rule results in exactly one of a behavior, a delegate "
                                    "selector, an extension point, a weight, or a weight fact.";
                return result;
            }
            if (weighs)
            {
                rule.Result = AnimRuleResultKind::Weight;
                if (weightFact != nullptr)
                    rule.WeightFact = *weightFact;
                else
                    rule.Weight = static_cast<float>(weight->AsNumber());
                if (rule.Weight < 0.0f || rule.Weight > 1.0f)
                {
                    result.Error = at + ".weight A layer weight is between 0 and 1.";
                    return result;
                }
                if (rule.HasStay || entry.Find("hold_min_ms") != nullptr || entry.Find("cooldown_ms") != nullptr)
                {
                    result.Error = at + " A weight rule is chosen by its enter alone each tick; it has no "
                                        "stay, hold or cooldown.";
                    return result;
                }
            }
            else if (behavior != nullptr)
            {
                GameplayTagError error;
                if (!tagSyntax.RegisterTag(*behavior, &error))
                {
                    result.Error = at + ".behavior " + error.Message;
                    return result;
                }
                rule.Result = AnimRuleResultKind::Behavior;
                rule.Behavior = *behavior;
            }
            else if (delegate != nullptr)
            {
                rule.Result = AnimRuleResultKind::Delegate;
                rule.Delegate = *delegate;
                result.Dependencies.push_back(AssetRef{ AssetType::Data, rule.Delegate });
            }
            else
            {
                rule.Result = AnimRuleResultKind::Extension;
                rule.Extension = *extension;
            }

            const auto duration = [&](std::string_view key) {
                const JsonValue* value = entry.Find(key);
                return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : 0.0f;
            };
            rule.HoldMinMs = duration("hold_min_ms");
            rule.CooldownMs = duration("cooldown_ms");
            if (rule.HoldMinMs < 0.0f || rule.CooldownMs < 0.0f)
            {
                result.Error = at + " Hold and cooldown are not negative.";
                return result;
            }
            selector->Rules.push_back(std::move(rule));
        }
        result.Value = std::move(selector);
        return result;
    }
}

void RegisterAnimSelectorData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimSelectorType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimSelectorType);
}
