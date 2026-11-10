#include <math/ConsiderationRule.h>

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace
{
    bool CompileRequire(const JsonValue& require, ConsiderationRule& rule, std::string_view subject,
                        std::vector<std::string>& errors)
    {
        if (const JsonValue* expect = require.Find("expect"))
        {
            rule.Min = rule.Max = expect->AsBool() ? 1.0f : 0.0f;
            return true;
        }
        rule.Min = static_cast<float>(require.NumberOr("min", -INFINITY));
        rule.Max = static_cast<float>(require.NumberOr("max", INFINITY));
        if (rule.Min > rule.Max)
        {
            errors.push_back(std::format("{}: require.min exceeds require.max", subject));
            return false;
        }
        return true;
    }

    bool CompileScore(const JsonValue& score, std::string_view missingCurve, ConsiderationRule& rule,
                      std::string_view subject, std::vector<std::string>& errors)
    {
        rule.Weight = static_cast<float>(score.NumberOr("weight", 1.0));
        if (!(rule.Weight > 0.0f) || !std::isfinite(rule.Weight))
        {
            errors.push_back(std::format("{}: a score weight is positive and finite", subject));
            return false;
        }
        if (const JsonValue* curve = score.Find("curve"))
        {
            std::string problem;
            rule.Curve = ReadResponseCurve(*curve, problem);
            if (!rule.Curve)
            {
                errors.push_back(std::format("{}: {}", subject, problem));
                return false;
            }
        }
        else if (!missingCurve.empty())
        {
            errors.push_back(std::format("{}: {}", subject, missingCurve));
            return false;
        }
        return true;
    }

    [[nodiscard]] DataFieldSchema OptionalField(DataFieldKind kind, std::string key, std::string display)
    {
        return DataField::Field(std::move(key), kind, std::move(display), {}, false);
    }
}

float ConsiderationRule::Output(float value) const
{
    return Curve ? EvaluateResponseCurve(*Curve, value) : std::clamp(value, 0.0f, 1.0f);
}

bool CompileConsiderationRule(const JsonValue& step, std::string_view missingCurve, ConsiderationRule& out,
                              std::string_view subject, std::vector<std::string>& errors)
{
    const JsonValue* require = step.Find("require");
    const JsonValue* score = step.Find("score");
    if ((require == nullptr) == (score == nullptr))
    {
        errors.push_back(std::format("{}: exactly one of 'require' and 'score'", subject));
        return false;
    }
    out.Mode = require != nullptr ? ConsiderationMode::Require : ConsiderationMode::Score;
    const bool ok = require != nullptr ? CompileRequire(*require, out, subject, errors)
                                       : CompileScore(*score, missingCurve, out, subject, errors);
    if (const JsonValue* unmeasured = step.Find("unmeasured"))
        out.UnmeasuredScore = static_cast<float>(unmeasured->NumberOr("score", 0.0));
    return ok;
}

std::vector<DataFieldSchema> MakeConsiderationRuleFields(std::string unmeasuredSummary)
{
    DataFieldSchema require = OptionalField(DataFieldKind::Record, "require", "Require");
    require.Children.push_back(OptionalField(DataFieldKind::Float, "min", "Minimum"));
    require.Children.push_back(OptionalField(DataFieldKind::Float, "max", "Maximum"));
    require.Children.push_back(OptionalField(DataFieldKind::Bool, "expect", "Expect"));

    DataFieldSchema score = OptionalField(DataFieldKind::Record, "score", "Score");
    DataFieldSchema curve = MakeResponseCurveField("curve", "Curve");
    curve.Required = false;
    score.Children.push_back(std::move(curve));
    DataFieldSchema weight = OptionalField(DataFieldKind::Float, "weight", "Weight");
    weight.Default = 1.0;
    score.Children.push_back(std::move(weight));

    DataFieldSchema unmeasured = OptionalField(DataFieldKind::Record, "unmeasured", "When unmeasured");
    unmeasured.Summary = std::move(unmeasuredSummary);
    DataFieldSchema unmeasuredScore = DataField::Field("score", DataFieldKind::Float, "Score", {});
    unmeasuredScore.Numeric.Minimum = 0.0;
    unmeasuredScore.Numeric.Maximum = 1.0;
    unmeasured.Children.push_back(std::move(unmeasuredScore));

    std::vector<DataFieldSchema> fields;
    fields.push_back(std::move(require));
    fields.push_back(std::move(score));
    fields.push_back(std::move(unmeasured));
    return fields;
}
