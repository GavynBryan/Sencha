#include <logic/selection/SelectionTableDesc.h>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    constexpr std::string_view kQuerier = "querier";

    [[nodiscard]] std::optional<std::uint8_t> IndexOf(const std::vector<std::string>& names, std::string_view name)
    {
        const auto found = std::ranges::find(names, name);
        if (found == names.end())
            return std::nullopt;
        return static_cast<std::uint8_t>(found - names.begin());
    }

    bool CompileNames(const JsonValue& data, std::string_view key, std::vector<std::string>& out,
                      std::size_t limit, std::vector<std::string>& errors)
    {
        const JsonValue* names = data.Find(key);
        if (names == nullptr)
            return true;
        bool ok = true;
        for (const JsonValue& name : names->AsArray())
        {
            if (IndexOf(out, name.AsString()))
            {
                errors.push_back(std::format("{} '{}' is declared twice", key, name.AsString()));
                ok = false;
                continue;
            }
            out.push_back(name.AsString());
        }
        if (out.size() > limit)
        {
            errors.push_back(std::format("a table declares at most {} {}", limit, key));
            ok = false;
        }
        return ok;
    }

    bool CompileSource(const JsonValue& step, const SelectionTableDesc& table, SelectionConsiderationDesc& out,
                       std::string_view subject, std::vector<std::string>& errors)
    {
        const JsonValue* input = step.Find("input");
        const JsonValue* query = step.Find("query");
        if ((input == nullptr) == (query == nullptr))
        {
            errors.push_back(std::format("{}: exactly one of 'input' and 'query'", subject));
            return false;
        }
        if (input != nullptr)
        {
            const std::optional<std::uint8_t> index = IndexOf(table.Inputs, input->AsString());
            if (!index)
            {
                errors.push_back(std::format("{}: '{}' is not an input this table declares", subject,
                                             input->AsString()));
                return false;
            }
            out.Source = SelectionSource::Input;
            out.Input = *index;
            return true;
        }

        out.Source = SelectionSource::Query;
        out.Query = query->AsString();
        const JsonValue* constants = step.Find("arguments");
        out.Constants = constants != nullptr ? *constants : JsonValue(JsonValue::Object{});
        bool ok = true;
        if (const JsonValue* entities = step.Find("entities"))
        {
            for (const auto& [argument, entity] : entities->AsObject())
            {
                const std::optional<std::uint8_t> index = IndexOf(table.Entities, entity.AsString());
                if (!index)
                {
                    errors.push_back(std::format("{}: '{}' is not an entity this table declares", subject,
                                                 entity.AsString()));
                    ok = false;
                    continue;
                }
                if (out.Constants.Find(argument) != nullptr)
                {
                    errors.push_back(std::format("{}: argument '{}' is both a constant and an entity", subject,
                                                 argument));
                    ok = false;
                    continue;
                }
                out.EntityArguments.push_back({ argument, *index });
            }
        }
        return ok;
    }

    bool CompileConsideration(const JsonValue& step, const SelectionTableDesc& table,
                              SelectionConsiderationDesc& out, std::string_view subject,
                              std::vector<std::string>& errors)
    {
        if (!CompileSource(step, table, out, subject, errors))
            return false;
        return CompileConsiderationRule(step,
                                        out.Source == SelectionSource::Input ? "scoring an input needs a curve" : "",
                                        out.Rule, subject, errors);
    }

    bool CompileOptions(const JsonValue& data, SelectionTableDesc& out, std::vector<std::string>& errors)
    {
        const JsonValue& options = *data.Find("options");
        if (options.AsArray().empty() || options.AsArray().size() > kMaxSelectionOptions)
        {
            errors.push_back(std::format("a table declares between 1 and {} options", kMaxSelectionOptions));
            return false;
        }
        bool ok = true;
        for (std::size_t index = 0; index < options.AsArray().size(); ++index)
        {
            const JsonValue& authored = options.AsArray()[index];
            SelectionOptionDesc option;
            option.Name = authored.Find("name")->AsString();
            option.CommitSeconds = static_cast<float>(authored.NumberOr("commit_seconds", 0.0));
            const std::string subject = std::format("option {} '{}'", index, option.Name);
            if (std::ranges::any_of(out.Options, [&](const SelectionOptionDesc& other) { return other.Name == option.Name; }))
            {
                errors.push_back(std::format("{}: the name is used twice", subject));
                ok = false;
            }
            if (const JsonValue* considerations = authored.Find("considerations"))
            {
                if (considerations->AsArray().size() > kMaxSelectionConsiderations)
                {
                    errors.push_back(std::format("{}: at most {} considerations", subject, kMaxSelectionConsiderations));
                    ok = false;
                    continue;
                }
                for (std::size_t step = 0; step < considerations->AsArray().size(); ++step)
                {
                    SelectionConsiderationDesc consideration;
                    ok = CompileConsideration(considerations->AsArray()[step], out, consideration,
                                              std::format("{} consideration {}", subject, step), errors)
                        && ok;
                    option.Considerations.push_back(std::move(consideration));
                }
            }
            out.Options.push_back(std::move(option));
        }
        return ok;
    }

    void CompileSelection(const JsonValue& data, SelectionTableDesc& out)
    {
        const JsonValue* selection = data.Find("selection");
        if (selection == nullptr)
            return;
        out.Mode = selection->Find("mode")->AsString() == "pick_from_band" ? SelectionMode::PickFromBand
                                                                           : SelectionMode::Best;
        out.BandWidth = static_cast<float>(selection->NumberOr("band_width", 0.0));
    }

    [[nodiscard]] DataFieldSchema Optional(DataFieldKind kind, std::string key, std::string displayName,
                                           std::string tooltip = {})
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(displayName), std::move(tooltip));
        field.Required = false;
        return field;
    }

    [[nodiscard]] DataFieldSchema OpenRecord(std::string key, std::string displayName)
    {
        DataFieldSchema field = Optional(DataFieldKind::Record, std::move(key), std::move(displayName));
        return field;
    }

    [[nodiscard]] DataFieldSchema NameList(std::string key, std::string displayName, std::string tooltip)
    {
        DataFieldSchema list = Optional(DataFieldKind::Array, std::move(key), std::move(displayName), std::move(tooltip));
        list.Children.push_back(MakeDataField(DataFieldKind::String, "name", "Name"));
        return list;
    }
}

bool CompileSelectionTableDesc(const JsonValue& data, SelectionTableDesc& out, std::vector<std::string>& errors)
{
    std::vector<DataValidationError> problems;
    if (!ValidateDataAgainstSchema(data, MakeSelectionTableSchema(), problems))
    {
        errors.push_back(FormatDataValidationErrors(problems));
        return false;
    }

    out = SelectionTableDesc{};
    const JsonValue* name = data.Find("name");
    out.Name = name != nullptr ? name->AsString() : std::string{};
    out.Entities.push_back(std::string(kQuerier));
    out.SwitchMargin = static_cast<float>(data.NumberOr("switch_margin", 0.0));
    if (const JsonValue* interrupt = data.Find("interrupt"))
        out.Interrupt = ReadTagQueryNames(*interrupt);

    bool ok = CompileNames(data, "entities", out.Entities, 255, errors);
    ok = CompileNames(data, "inputs", out.Inputs, 255, errors) && ok;
    CompileSelection(data, out);
    ok = CompileOptions(data, out, errors) && ok;
    return ok;
}

DataSchema MakeSelectionTableSchema()
{
    DataFieldSchema consideration = MakeDataField(DataFieldKind::Record, "consideration", "Consideration");
    consideration.Children.push_back(Optional(DataFieldKind::String, "input", "Input",
                                              "A number the caller supplies, named in the table's inputs."));
    consideration.Children.push_back(Optional(DataFieldKind::String, "query", "Query",
                                              "An authored query with a numeric answer."));
    consideration.Children.push_back(OpenRecord("entities", "Entity arguments"));
    consideration.Children.push_back(OpenRecord("arguments", "Constant arguments"));
    for (DataFieldSchema& field : MakeConsiderationRuleFields("Score an unmeasured value instead of disqualifying the option."))
        consideration.Children.push_back(std::move(field));

    DataFieldSchema considerations = Optional(DataFieldKind::Array, "considerations", "Considerations");
    considerations.Editor.Widget = "cards";
    considerations.Children.push_back(std::move(consideration));

    DataFieldSchema option = MakeDataField(DataFieldKind::Record, "option", "Option");
    option.Children.push_back(MakeDataField(DataFieldKind::String, "name", "Name"));
    DataFieldSchema commit = Optional(DataFieldKind::Float, "commit_seconds", "Commit seconds",
                                      "How long a chosen option is kept before the table reconsiders it.");
    commit.Numeric.Minimum = 0.0;
    option.Children.push_back(std::move(commit));
    option.Children.push_back(std::move(considerations));

    DataFieldSchema options = MakeDataField(DataFieldKind::Array, "options", "Options");
    options.Editor.Widget = "cards";
    options.Children.push_back(std::move(option));

    DataFieldSchema mode = MakeDataField(DataFieldKind::Enum, "mode", "Mode");
    for (const char* choice : { "best", "pick_from_band" })
        mode.EnumChoices.push_back(DataEnumChoice{ choice, choice, {} });
    DataFieldSchema selection = OpenRecord("selection", "Selection");
    selection.Children.push_back(std::move(mode));
    DataFieldSchema bandWidth = Optional(DataFieldKind::Float, "band_width", "Band width");
    bandWidth.Numeric.Minimum = 0.0;
    selection.Children.push_back(std::move(bandWidth));

    DataFieldSchema margin = Optional(DataFieldKind::Float, "switch_margin", "Switch margin",
                                      "How much more another option must score to replace the current one.");
    margin.Numeric.Minimum = 0.0;

    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    root.Children.push_back(Optional(DataFieldKind::String, "name", "Name"));
    root.Children.push_back(NameList("entities", "Entities", "Entities the caller binds besides the querier."));
    root.Children.push_back(NameList("inputs", "Inputs", "Numbers the caller supplies."));
    root.Children.push_back(std::move(margin));
    root.Children.push_back(std::move(selection));
    root.Children.push_back(MakeTagQueryNamesField("interrupt", "Interrupt"));
    root.Children.push_back(std::move(options));

    DataSchema schema;
    schema.TypeName = std::string(kSelectionTableTypeName);
    schema.DisplayName = "Selection table";
    schema.Description = "Which of several options a querier takes, scored by curves over inputs and queries.";
    schema.Root = std::move(root);
    schema.AllowUnknownFields = true;
    return schema;
}
