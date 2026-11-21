#include <authored/VocabularyReference.h>

#include <authored/AuthoredClassification.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRef.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

namespace
{
    constexpr std::string_view kNone = "—";

    std::string Cell(std::string_view text)
    {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char c : text)
        {
            if (c == '|')
                escaped += "\\|";
            else if (c == '\n')
                escaped += ' ';
            else
                escaped += c;
        }
        return escaped.empty() ? std::string(kNone) : escaped;
    }

    std::string NumberText(double value)
    {
        return std::format("{}", value);
    }

    std::string DefaultText(const DataDefaultValue& value)
    {
        if (const bool* flag = std::get_if<bool>(&value))
            return *flag ? "true" : "false";
        if (const std::int64_t* whole = std::get_if<std::int64_t>(&value))
            return std::to_string(*whole);
        if (const double* number = std::get_if<double>(&value))
            return NumberText(*number);
        if (const std::string* text = std::get_if<std::string>(&value))
            return std::format("\"{}\"", *text);
        return {};
    }

    std::string RangeText(const DataNumericConstraints& numeric)
    {
        if (!numeric.Minimum && !numeric.Maximum)
            return {};
        const std::string low = numeric.Minimum ? NumberText(*numeric.Minimum) + " " : std::string();
        const std::string high = numeric.Maximum ? " " + NumberText(*numeric.Maximum) : std::string();
        return std::format(" [{}..{}]", low, high);
    }

    std::string FieldText(const DataFieldSchema& field);

    std::string MembersText(const DataFieldSchema& record, std::string_view separator)
    {
        std::string text;
        for (const DataFieldSchema& member : record.Children)
        {
            if (!text.empty())
                text += separator;
            text += FieldText(member);
        }
        return text;
    }

    std::string TypeText(const DataFieldSchema& field)
    {
        switch (field.Kind)
        {
        case DataFieldKind::Enum:
        {
            std::string choices;
            for (const DataEnumChoice& choice : field.EnumChoices)
                choices += (choices.empty() ? "" : ", ") + choice.Value;
            return std::format("{}({})", DataFieldKindName(field.Kind), choices);
        }
        case DataFieldKind::Vector:
            return std::format("{}{}", DataFieldKindName(field.Kind), field.VectorLength);
        case DataFieldKind::AssetRef:
            if (field.Reference.AssetTypeFilter != AssetType::Unknown)
                return std::format("{} ({})", DataFieldKindName(field.Kind),
                                   AssetTypeToString(field.Reference.AssetTypeFilter));
            break;
        case DataFieldKind::DataAssetRef:
            if (!field.Reference.DataSubtype.empty())
                return std::format("{} ({})", DataFieldKindName(field.Kind), field.Reference.DataSubtype);
            break;
        case DataFieldKind::Record:
            return std::format("{{{}}}", MembersText(field, ", "));
        case DataFieldKind::Array:
        case DataFieldKind::Optional:
            if (field.Children.size() == 1)
            {
                const std::string element = TypeText(field.Children.front());
                return field.Kind == DataFieldKind::Array
                    ? std::format("{} of {}", DataFieldKindName(field.Kind), element)
                    : element;
            }
            break;
        default:
            break;
        }
        return std::string(DataFieldKindName(field.Kind));
    }

    std::string FieldText(const DataFieldSchema& field)
    {
        const bool hasDefault = !std::holds_alternative<std::monostate>(field.Default);
        const bool optional = field.Kind == DataFieldKind::Optional || (!field.Required && !hasDefault);
        const DataFieldSchema& value =
            field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
        std::string text = std::format("{}{}: {}", field.Key, optional ? "?" : "", TypeText(field));
        if (hasDefault)
            text += " = " + DefaultText(field.Default);
        text += RangeText(value.Numeric);
        if (!value.Units.empty())
            text += std::format(" ({})", value.Units);
        return text;
    }

    std::string ArgumentsCell(const DataFieldSchema& record)
    {
        std::string cell;
        for (const DataFieldSchema& member : record.Children)
        {
            if (!cell.empty())
                cell += "<br>";
            cell += std::format("`{}`", Cell(FieldText(member)));
        }
        return cell.empty() ? std::string(kNone) : cell;
    }

    template<typename Catalog>
    std::vector<const typename Catalog::Definition*> Declared(const Catalog* catalog, std::string_view provider)
    {
        std::vector<const typename Catalog::Definition*> declared;
        if (catalog == nullptr)
            return declared;
        for (const auto id : catalog->Live())
        {
            if (provider.empty() || catalog->Provider(id) == provider)
                declared.push_back(catalog->Get(id));
        }
        std::ranges::sort(declared, {}, [](const auto* definition) { return std::string_view(definition->Name); });
        return declared;
    }

    std::string_view VerbKindText(const VerbDefinition& verb)
    {
        if (verb.Kind == VerbKind::Instant)
            return "instant";
        return verb.Completes ? "lease, completes" : "lease";
    }

    void AppendVerbs(std::string& out, const VerbRegistry* verbs, std::string_view provider)
    {
        out += "\n## Verbs\n\n"
               "| Verb | Kind | Scope / prediction | Arguments | Description |\n"
               "|---|---|---|---|---|\n";
        for (const VerbDefinition* verb : Declared(verbs, provider))
        {
            std::format_to(std::back_inserter(out), "| `{}` | {} | {} / {} | {} | {} |\n", verb->Name,
                           VerbKindText(*verb), AuthoredScopeName(verb->Scope),
                           AuthoredPredictionName(verb->Prediction), ArgumentsCell(verb->Arguments),
                           Cell(verb->Description));
        }
    }

    void AppendQueries(std::string& out, const AuthoredQueryRegistry* queries, std::string_view provider)
    {
        out += "\n## Queries\n\n"
               "| Query | Arguments | Result | Description |\n"
               "|---|---|---|---|\n";
        for (const AuthoredQueryDefinition* query : Declared(queries, provider))
        {
            std::format_to(std::back_inserter(out), "| `{}` | {} | `{}` | {} |\n", query->Name,
                           ArgumentsCell(query->Arguments), Cell(TypeText(query->Result)), Cell(query->Description));
        }
    }

    void AppendEvents(std::string& out, const AuthoredEventRegistry* events, std::string_view provider)
    {
        out += "\n## Events\n\n"
               "| Event | Source | Payload | Instigator | Description |\n"
               "|---|---|---|---|---|\n";
        for (const AuthoredEventDefinition* event : Declared(events, provider))
        {
            std::format_to(std::back_inserter(out), "| `{}` | {} | {} | {} | {} |\n", event->Name,
                           Cell(event->SourceComponent), ArgumentsCell(event->Payload), Cell(event->Instigator),
                           Cell(event->Description));
        }
    }
}

std::string RenderVocabularyReference(const World& names, std::string_view provider)
{
    std::string out = provider.empty() ? std::string("# Authored vocabulary\n")
                                       : std::format("# Authored vocabulary: `{}`\n", provider);
    out += "\nGenerated from the World's catalog by `RenderVocabularyReference` "
           "(`authored/VocabularyReference.h`); change a declaration, not this file.\n"
           "An argument reads `key: type = default [minimum .. maximum] (units)`; "
           "`key?` may be left out and has no default.\n";
    AppendVerbs(out, FindVerbRegistry(names), provider);
    AppendQueries(out, FindAuthoredQueryRegistry(names), provider);
    AppendEvents(out, FindAuthoredEventRegistry(names), provider);
    return out;
}
