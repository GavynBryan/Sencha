#include "authoring/AnimationPredicateText.h"

#include <anim/AnimSelectSystem.h>
#include <anim/AnimSelectorData.h>

#include <array>
#include <cmath>
#include <format>

namespace
{
    constexpr std::array<std::string_view, 6> kCompareSymbols{ "<", "<=", ">", ">=", "==", "!=" };
    constexpr std::array<std::string_view, 3> kMatchWords{ "all of", "any of", "none of" };
    constexpr std::array<std::string_view, 5> kReasonWords{ "any reason", "released", "interrupted", "failed",
                                                            "superseded" };

    std::string Number(double value)
    {
        std::string text = std::format("{:.3f}", value);
        while (text.back() == '0')
            text.pop_back();
        if (text.back() == '.')
            text.pop_back();
        return text;
    }

    std::string Comparison(const AnimPredicateTest& test, std::string_view unit = {})
    {
        const std::string operand = test.Tag.empty() ? Number(test.Value) + std::string(unit) : test.Tag;
        return std::format("{} {}", kCompareSymbols[static_cast<std::size_t>(test.Compare)], operand);
    }
}

std::string DescribeAnimPredicateTest(const AnimPredicateTest& test)
{
    std::string text;
    switch (test.Kind)
    {
    case AnimTestKind::Fact:
        text = test.HasCompare ? std::format("{} {}", test.Fact, Comparison(test)) : test.Fact;
        break;
    case AnimTestKind::Tags:
    {
        std::string query;
        for (const std::string& tag : test.Query)
            query += (query.empty() ? "" : ", ") + tag;
        text = std::format("{} has {} [{}]", test.Fact, kMatchWords[static_cast<std::size_t>(test.Match)], query);
        break;
    }
    case AnimTestKind::Request:
        switch (test.Test)
        {
        case AnimRequestTest::Active: text = std::format("request {}", test.Intent); break;
        case AnimRequestTest::Age: text = std::format("age of {} {}", test.Intent, Comparison(test, "s")); break;
        case AnimRequestTest::Param: text = std::format("{}.{} {}", test.Intent, test.Param, Comparison(test)); break;
        case AnimRequestTest::Cancelled:
            text = std::format("{} cancelled ({})", test.Intent, kReasonWords[static_cast<std::size_t>(test.Reason)]);
            break;
        }
        break;
    case AnimTestKind::Elapsed:
        text = std::format("time in behavior {}", Comparison(test, "s"));
        break;
    }
    if (!test.Negate)
        return text;
    const bool simple = test.Kind == AnimTestKind::Fact && !test.HasCompare;
    return simple ? "not " + text : "not (" + text + ")";
}

std::string DescribeAnimPredicateRow(const AnimPredicateRow& row)
{
    if (row.AnyOf.size() == 1)
        return DescribeAnimPredicateTest(row.AnyOf.front());
    std::string text;
    for (const AnimPredicateTest& test : row.AnyOf)
        text += (text.empty() ? "(" : " or ") + DescribeAnimPredicateTest(test);
    return text + ")";
}

std::string DescribeAnimPredicate(const AnimPredicateDecl& predicate)
{
    if (predicate.Rows.empty())
        return "always";
    std::string text;
    for (const AnimPredicateRow& row : predicate.Rows)
        text += (text.empty() ? "" : " and ") + DescribeAnimPredicateRow(row);
    return text;
}

std::string DescribeAnimPredicate(const JsonValue* rows)
{
    AnimPredicateDecl decl;
    std::string error;
    if (!ReadAnimPredicate(rows, "$", decl, error))
        return "invalid: " + error;
    return DescribeAnimPredicate(decl);
}

std::string DescribeAnimRowSource(const DataAssetCache& data, const AnimRowSource& source)
{
    const AnimSelectorData* selector = data.TryGet<AnimSelectorData>(data.Find(source.Selector), kAnimSelectorType);
    if (selector == nullptr || source.Rule >= selector->Rules.size())
        return "(source not loaded)";
    const AnimSelectorRuleDecl& rule = selector->Rules[source.Rule];
    const AnimPredicateDecl& predicate = source.Stay ? rule.Stay : rule.Enter;
    return source.Row < predicate.Rows.size() ? DescribeAnimPredicateRow(predicate.Rows[source.Row])
                                              : "(row not found)";
}

std::string DescribeAnimRuleRows(const DataAssetCache& data, const std::vector<AnimRowSource>& rows)
{
    if (rows.empty())
        return "always";
    std::string text;
    for (const AnimRowSource& row : rows)
        text += (text.empty() ? "" : " and ") + DescribeAnimRowSource(data, row);
    return text;
}

std::string DescribeAnimVerdict(const DataAssetCache& data, const AnimBoundRule& rule, const AnimRuleVerdict& verdict)
{
    std::string text(AnimRuleVerdictName(verdict.Kind));
    if (verdict.Kind == AnimRuleVerdictKind::Winner)
        return verdict.Stayed ? "winner, stayed" : "winner, entered";
    if (verdict.Kind != AnimRuleVerdictKind::Failed || verdict.Evaluation.FailedRow < 0)
        return text;
    const auto row = static_cast<std::size_t>(verdict.Evaluation.FailedRow);
    const std::vector<AnimRowSource>& rows = verdict.EvaluatedStay ? rule.StayRows : rule.EnterRows;
    if (verdict.EvaluatedStay)
        text = "stay failed";
    text += ": " + (row < rows.size() ? DescribeAnimRowSource(data, rows[row]) : std::string("(row)"));
    const double observed = verdict.Evaluation.Observed;
    if (std::isnan(observed))
        text += " (nothing to read)";
    else if (!std::isnan(verdict.Evaluation.Expected))
        text += std::format(" (read {:.3g})", observed);
    return text;
}
