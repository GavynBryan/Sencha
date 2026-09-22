#include <anim/AnimFactSchema.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <utility>

namespace
{
    constexpr std::array<std::string_view, 5> kKindNames{ "bool", "float", "int", "tag", "tagset" };
    constexpr std::array<std::string_view, 9> kOpNames{
        "edge", "time_since", "hysteresis", "min_duration", "smooth",
        "compare", "and", "or", "not" };
    constexpr std::array<std::string_view, 6> kCompareNames{ "lt", "le", "gt", "ge", "eq", "ne" };

    template <std::size_t N>
    std::optional<std::size_t> IndexOf(const std::array<std::string_view, N>& names,
                                       std::string_view value)
    {
        const auto it = std::find(names.begin(), names.end(), value);
        if (it == names.end())
            return std::nullopt;
        return static_cast<std::size_t>(it - names.begin());
    }

    DataFieldSchema Field(std::string key, DataFieldKind kind, std::string display,
                          std::string summary, bool required = true)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        field.DisplayName = std::move(display);
        field.Summary = std::move(summary);
        field.Required = required;
        return field;
    }

    DataFieldSchema EnumField(std::string key, std::string display, std::string summary,
                              std::vector<DataEnumChoice> choices, bool required = true)
    {
        DataFieldSchema field = Field(std::move(key), DataFieldKind::Enum, std::move(display),
                                      std::move(summary), required);
        field.EnumChoices = std::move(choices);
        return field;
    }

    DataFieldSchema OperandRecord(std::string key, bool required)
    {
        DataFieldSchema record = Field(std::move(key), DataFieldKind::Record, "Source",
                                       "The fact this derivation reads.", required);
        record.Children.push_back(Field("fact", DataFieldKind::String, "Fact",
                                        "A slot or an earlier derived fact."));
        record.Children.push_back(Field("not", DataFieldKind::Bool, "Negate",
                                        "Read the fact's opposite.", false));
        return record;
    }

    DataSchema MakeSchema()
    {
        DataFieldSchema slot = Field({}, DataFieldKind::Record, "Slot", {});
        slot.Children.push_back(Field("name", DataFieldKind::String, "Name",
                                      "The name rules and bindings address this slot by."));
        slot.Children.push_back(EnumField(
            "kind", "Kind", "What the slot holds.",
            { { "bool", "Boolean", "True or false" },
              { "float", "Float", "32-bit floating point" },
              { "int", "Integer", "Signed 32-bit integer" },
              { "tag", "Gameplay tag", "One tag, resolved per World" },
              { "tagset", "Tag set", "The entity's counted tag container" } }));
        slot.Children.push_back(Field("local", DataFieldKind::Bool, "Local only",
                                      "Exists only on the rendering machine; may not change timing "
                                      "other machines depend on.",
                                      false));
        DataFieldSchema slots = Field("slots", DataFieldKind::Array, "Slots",
                                      "Facts gameplay publishes.", false);
        slots.Editor.Widget = "cards";
        slots.Editor.TitleKey = "name";
        slots.Children.push_back(std::move(slot));

        DataFieldSchema derived = Field({}, DataFieldKind::Record, "Derived fact", {});
        derived.Children.push_back(Field("name", DataFieldKind::String, "Name",
                                         "The derived fact's own name."));
        derived.Children.push_back(EnumField(
            "op", "Derivation", "The closed set of derivations.",
            { { "edge", "Edge", "True for a while after the source changes" },
              { "time_since", "Time since", "Seconds since the source last had a value" },
              { "hysteresis", "Hysteresis", "Rises at Enter, falls at Exit" },
              { "min_duration", "Minimum duration", "The source has held true this long" },
              { "smooth", "Smooth", "Exponentially smoothed value" },
              { "compare", "Compare", "The source against a constant" },
              { "and", "All of", "Every source true" },
              { "or", "Any of", "Any source true" },
              { "not", "Not", "The source false" } }));
        derived.Children.push_back(OperandRecord("source", false));
        DataFieldSchema sources = Field("sources", DataFieldKind::Array, "Sources",
                                        "Two or more operands, for All of and Any of.", false);
        sources.Children.push_back(OperandRecord({}, true));
        derived.Children.push_back(std::move(sources));
        derived.Children.push_back(EnumField("direction", "Direction", "Which edge.",
                                             { { "rising", "Rising", "False to true" },
                                               { "falling", "Falling", "True to false" } },
                                             false));
        DataFieldSchema window = Field("window_ms", DataFieldKind::Float, "Window (ms)",
                                       "Hold, cap, minimum duration, or smoothing time constant.",
                                       false);
        window.Numeric.Minimum = 0.0;
        window.Numeric.Maximum = kAnimMaxDerivationWindowMs;
        window.Units = "ms";
        derived.Children.push_back(std::move(window));
        derived.Children.push_back(Field("value", DataFieldKind::Bool, "Value",
                                         "Time since the source was last this.", false));
        derived.Children.push_back(Field("enter", DataFieldKind::Float, "Enter",
                                         "Rises when the source reaches this.", false));
        derived.Children.push_back(Field("exit", DataFieldKind::Float, "Exit",
                                         "Falls when the source drops to this.", false));
        derived.Children.push_back(EnumField(
            "compare", "Comparison", "How the source is compared.",
            { { "lt", "<", {} }, { "le", "<=", {} }, { "gt", ">", {} },
              { "ge", ">=", {} }, { "eq", "==", {} }, { "ne", "!=", {} } },
            false));
        derived.Children.push_back(Field("constant", DataFieldKind::Float, "Constant",
                                         "What the source is compared against.", false));
        DataFieldSchema derivedList = Field("derived", DataFieldKind::Array, "Derived facts",
                                            "Facts computed from other facts.", false);
        derivedList.Editor.Widget = "cards";
        derivedList.Editor.TitleKey = "name";
        derivedList.Children.push_back(std::move(derived));

        DataFieldSchema extends = Field("extends", DataFieldKind::DataAssetRef, "Extends",
                                        "The schema whose slots come first.", false);
        extends.Reference.DataSubtype = std::string(kAnimFactSchemaType);

        DataSchema schema;
        schema.TypeName = std::string(kAnimFactSchemaType);
        schema.DisplayName = "Animation fact schema";
        schema.Description =
            "Typed slots gameplay publishes for animation rules, and facts derived from them.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(extends));
        schema.Root.Children.push_back(std::move(slots));
        schema.Root.Children.push_back(std::move(derivedList));
        return schema;
    }

    bool ReadOperand(const JsonValue& value, AnimFactOperand& out)
    {
        const JsonValue* fact = value.Find("fact");
        if (fact == nullptr || !fact->IsString())
            return false;
        out.Fact = fact->AsString();
        const JsonValue* negate = value.Find("not");
        out.Negate = negate != nullptr && negate->IsBool() && negate->AsBool();
        return true;
    }

    float NumberOr(const JsonValue& value, std::string_view key, float fallback)
    {
        const JsonValue* found = value.Find(key);
        return found != nullptr && found->IsNumber() ? static_cast<float>(found->AsNumber())
                                                     : fallback;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto schema = std::make_shared<AnimFactSchema>();
        std::vector<std::string> names;

        const auto claim = [&](const std::string& name, const std::string& path) {
            if (!IsValidAnimFactName(name))
            {
                result.Error = path + " Use a name of letters, digits, underscores and dots, "
                                      "starting with a letter or underscore.";
                return false;
            }
            if (std::find(names.begin(), names.end(), name) != names.end())
            {
                result.Error = path + " '" + name + "' is declared twice in this schema.";
                return false;
            }
            names.push_back(name);
            return true;
        };

        if (const JsonValue* extends = data.Find("extends");
            extends != nullptr && extends->IsString() && !extends->AsString().empty())
        {
            schema->Extends = extends->AsString();
            result.Dependencies.push_back(AssetRef{ AssetType::Data, schema->Extends });
        }

        if (const JsonValue* slots = data.Find("slots"); slots != nullptr)
        {
            for (std::size_t i = 0; i < slots->AsArray().size(); ++i)
            {
                const JsonValue& entry = slots->AsArray()[i];
                const std::string path = std::format("$.data.slots[{}]", i);
                AnimFactSlotDecl slot;
                slot.Name = entry.Find("name")->AsString();
                if (!claim(slot.Name, path + ".name"))
                    return result;
                slot.Kind = static_cast<AnimFactKind>(
                    *IndexOf(kKindNames, entry.Find("kind")->AsString()));
                const JsonValue* local = entry.Find("local");
                slot.Local = local != nullptr && local->AsBool();
                schema->Slots.push_back(std::move(slot));
            }
        }

        if (const JsonValue* derived = data.Find("derived"); derived != nullptr)
        {
            for (std::size_t i = 0; i < derived->AsArray().size(); ++i)
            {
                const JsonValue& entry = derived->AsArray()[i];
                const std::string path = std::format("$.data.derived[{}]", i);
                AnimDerivedFactDecl fact;
                fact.Name = entry.Find("name")->AsString();
                if (!claim(fact.Name, path + ".name"))
                    return result;
                fact.Op = static_cast<AnimDerivationOp>(
                    *IndexOf(kOpNames, entry.Find("op")->AsString()));

                const bool combines = fact.Op == AnimDerivationOp::And
                    || fact.Op == AnimDerivationOp::Or;
                if (combines)
                {
                    const JsonValue* sources = entry.Find("sources");
                    if (sources == nullptr || sources->AsArray().size() < 2)
                    {
                        result.Error = path + ".sources All of and Any of take two or more sources.";
                        return result;
                    }
                    for (const JsonValue& source : sources->AsArray())
                    {
                        AnimFactOperand operand;
                        (void)ReadOperand(source, operand);
                        fact.Sources.push_back(std::move(operand));
                    }
                }
                else
                {
                    const JsonValue* source = entry.Find("source");
                    AnimFactOperand operand;
                    if (source == nullptr || !ReadOperand(*source, operand))
                    {
                        result.Error = path + ".source This derivation reads one source fact.";
                        return result;
                    }
                    fact.Sources.push_back(std::move(operand));
                }
                for (const AnimFactOperand& operand : fact.Sources)
                {
                    if (operand.Fact == fact.Name)
                    {
                        result.Error = path + ".source A derived fact cannot read itself.";
                        return result;
                    }
                }

                fact.WindowMs = NumberOr(entry, "window_ms", 0.0f);
                if (fact.IsTemporal())
                {
                    if (!(fact.WindowMs > 0.0f) || fact.WindowMs > kAnimMaxDerivationWindowMs)
                    {
                        result.Error = std::format(
                            "{}.window_ms A temporal derivation needs a window above 0 and at "
                            "most {} ms; that bound is what makes a late joiner's facts exact.",
                            path, kAnimMaxDerivationWindowMs);
                        return result;
                    }
                }
                if (const JsonValue* direction = entry.Find("direction"))
                    fact.Rising = direction->AsString() == "rising";
                if (const JsonValue* value = entry.Find("value"))
                    fact.MatchValue = value->AsBool();
                fact.Enter = NumberOr(entry, "enter", 0.0f);
                fact.Exit = NumberOr(entry, "exit", 0.0f);
                if (fact.Op == AnimDerivationOp::Hysteresis && !(fact.Enter > fact.Exit))
                {
                    result.Error = path + ".enter Hysteresis rises at Enter and falls at Exit; "
                                          "Enter must be above Exit.";
                    return result;
                }
                if (fact.Op == AnimDerivationOp::Compare)
                {
                    const JsonValue* compare = entry.Find("compare");
                    if (compare == nullptr || entry.Find("constant") == nullptr)
                    {
                        result.Error = path + " Compare needs a comparison and a constant.";
                        return result;
                    }
                    fact.Compare = static_cast<AnimCompareOp>(
                        *IndexOf(kCompareNames, compare->AsString()));
                    fact.Constant = NumberOr(entry, "constant", 0.0f);
                }
                schema->Derived.push_back(std::move(fact));
            }
        }

        result.Value = std::move(schema);
        return result;
    }
}

std::string_view AnimDerivationOpName(AnimDerivationOp op)
{
    return kOpNames[static_cast<std::size_t>(op)];
}

AnimFactKind AnimDerivedFactDecl::ResultKind() const
{
    return Op == AnimDerivationOp::TimeSince || Op == AnimDerivationOp::Smooth
        ? AnimFactKind::Float
        : AnimFactKind::Bool;
}

bool AnimDerivedFactDecl::IsTemporal() const
{
    return Op == AnimDerivationOp::Edge || Op == AnimDerivationOp::TimeSince
        || Op == AnimDerivationOp::MinDuration || Op == AnimDerivationOp::Smooth;
}

bool IsValidAnimFactName(std::string_view name)
{
    if (name.empty())
        return false;
    const auto start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (!start(name.front()))
        return false;
    char previous = '\0';
    for (const char c : name)
    {
        const bool body = start(c) || (c >= '0' && c <= '9') || c == '.';
        if (!body || (c == '.' && previous == '.'))
            return false;
        previous = c;
    }
    return name.back() != '.';
}

void RegisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimFactSchemaType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimFactSchemaType);
}

void UnregisterAnimFactSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kAnimFactSchemaType))
        (void)schemas.Unregister(kAnimFactSchemaType);
}
