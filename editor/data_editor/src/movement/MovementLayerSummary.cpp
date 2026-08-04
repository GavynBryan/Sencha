#include "MovementLayerSummary.h"

#include <movement/MovementProfileRuntime.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace
{
    const DataFieldSchema* FindChild(const DataFieldSchema& parent, std::string_view key)
    {
        const auto found = std::find_if(
            parent.Children.begin(), parent.Children.end(),
            [key](const DataFieldSchema& child) { return child.Key == key; });
        return found == parent.Children.end() ? nullptr : &*found;
    }

    void AppendClause(std::string& sentence, std::string_view clause)
    {
        if (!sentence.empty())
            sentence += " and ";
        sentence += clause;
    }

    std::string JoinTags(const std::vector<std::string>& tags, std::string_view conjunction)
    {
        std::string joined;
        for (std::size_t index = 0; index < tags.size(); ++index)
        {
            if (index > 0)
                joined += index + 1 == tags.size() ? std::format(" {} ", conjunction) : ", ";
            joined += tags[index];
        }
        return joined;
    }

    // Clause order is fixed so two layers with the same conditions always read
    // identically, whatever order the keys appear in the file.
    std::string DescribeClauses(const MovementLayerCondition& condition)
    {
        std::string sentence;

        if (condition.Mode)
            AppendClause(sentence, std::format("in mode '{}'", *condition.Mode));

        switch (condition.Support)
        {
        case MovementSupportCondition::Any:
            break;
        case MovementSupportCondition::None:
            AppendClause(sentence, "in the air");
            break;
        case MovementSupportCondition::Stable:
            AppendClause(sentence, "on stable ground");
            break;
        case MovementSupportCondition::Steep:
            AppendClause(sentence, "on steep ground");
            break;
        }

        if (condition.ImmersionAtLeast)
        {
            AppendClause(sentence, std::format("immersed at least {}%",
                FormatMovementCoefficient(*condition.ImmersionAtLeast * 100.0f)));
        }

        if (!condition.AllTags.empty())
            AppendClause(sentence, std::format("while {}", JoinTags(condition.AllTags, "and")));
        if (!condition.AnyTags.empty())
            AppendClause(sentence, std::format("while any of {}", JoinTags(condition.AnyTags, "or")));
        if (!condition.NoneTags.empty())
            AppendClause(sentence, std::format("unless {}", JoinTags(condition.NoneTags, "or")));

        return sentence;
    }

    void AppendPatch(std::string& summary,
                     const MovementTuningPatch& patch,
                     std::string_view op,
                     const std::vector<MovementCoefficientLabel>& labels)
    {
        for (const MovementTuningField& field : MovementTuningFields())
        {
            const std::optional<float>& value = patch.*field.Patch;
            if (!value)
                continue;

            const auto label = std::find_if(
                labels.begin(), labels.end(),
                [&field](const MovementCoefficientLabel& entry) { return entry.Key == field.Key; });
            const std::string display = label == labels.end()
                ? std::string(field.Key) : label->Display;
            const std::string units = label == labels.end() ? std::string{} : label->Units;

            if (!summary.empty())
                summary += ", ";
            summary += std::format("{} {} {}", display, op, FormatMovementCoefficient(*value));

            // A multiplier is dimensionless whatever the coefficient measures,
            // so only replacements and offsets carry the unit through.
            if (!units.empty() && op != "x")
                summary += " " + units;
        }
    }
}

std::vector<MovementCoefficientLabel> MovementCoefficientLabels(const DataSchema& schema)
{
    const DataFieldSchema* layers = FindChild(schema.Root, "layers");
    if (layers == nullptr || layers->Children.empty())
        return {};
    const DataFieldSchema* patch = FindChild(layers->Children.front(), "set");
    if (patch == nullptr)
        return {};

    std::vector<MovementCoefficientLabel> labels;
    labels.reserve(patch->Children.size());
    for (const DataFieldSchema& field : patch->Children)
    {
        labels.push_back(MovementCoefficientLabel{
            .Key = field.Key,
            .Display = field.DisplayName.empty() ? field.Key : field.DisplayName,
            .Units = field.Units,
        });
    }
    return labels;
}

std::string DescribeMovementCondition(const MovementLayerCondition& condition)
{
    const std::string clauses = DescribeClauses(condition);
    return clauses.empty() ? "Always" : "When " + clauses;
}

std::string SynthesizeMovementLayerName(const MovementLayerCondition& condition)
{
    std::string clauses = DescribeClauses(condition);
    if (clauses.empty())
        return "Always";
    clauses[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(clauses[0])));
    return clauses;
}

std::string MovementLayerName(const MovementProfileLayer& layer)
{
    return layer.Name.empty() ? SynthesizeMovementLayerName(layer.When) : layer.Name;
}

std::string DescribeMovementPatches(const MovementProfileLayer& layer,
                                    const std::vector<MovementCoefficientLabel>& labels)
{
    std::string summary;
    AppendPatch(summary, layer.Set, "=", labels);
    AppendPatch(summary, layer.Scale, "x", labels);
    AppendPatch(summary, layer.Add, "+", labels);
    return summary;
}

std::string FormatMovementCoefficient(float value)
{
    std::string text = std::format("{:.3f}", value);
    if (text.find('.') == std::string::npos)
        return text;

    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text.empty() ? "0" : text;
}
