#include <spatial/candidates/CandidateEvaluationDesc.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace
{
    const JsonValue kNoArguments = JsonValue(JsonValue::Object{});

    struct SelectionName
    {
        std::string_view Name;
        CandidateSelectionMode Mode;
    };

    constexpr SelectionName kSelectionNames[] = {
        { "best", CandidateSelectionMode::Best },
        { "top_n", CandidateSelectionMode::TopN },
        { "all_qualified", CandidateSelectionMode::AllQualified },
        { "pick_from_band", CandidateSelectionMode::PickFromBand },
    };

    [[nodiscard]] std::uint32_t CountOr(const JsonValue& object, std::string_view key, std::uint32_t fallback)
    {
        return static_cast<std::uint32_t>(object.NumberOr(key, fallback));
    }

    [[nodiscard]] const JsonValue& ArgumentsOf(const JsonValue& step)
    {
        const JsonValue* arguments = step.Find("arguments");
        return arguments != nullptr ? *arguments : kNoArguments;
    }

    // The operation's own schema; the envelope schema cannot know it.
    [[nodiscard]] bool CheckArguments(const JsonValue& arguments,
                                      const DataFieldSchema& declared,
                                      bool open,
                                      std::string_view subject,
                                      std::vector<std::string>& errors)
    {
        DataSchema schema;
        schema.Root = declared;
        schema.AllowUnknownFields = open;
        std::vector<DataValidationError> problems;
        if (ValidateDataAgainstSchema(arguments, schema, problems))
            return true;
        errors.push_back(std::format("{}: {}", subject, FormatDataValidationErrors(problems)));
        return false;
    }

    [[nodiscard]] std::uint32_t CriterionRank(const CandidateCriterion& criterion)
    {
        return static_cast<std::uint32_t>(criterion.Cost) * 2u
            + (criterion.Mode == CandidateCriterionMode::Score ? 1u : 0u);
    }

    bool CompileSlots(const JsonValue& data, CandidateEvaluationDesc& out, std::vector<std::string>& errors)
    {
        out.Slots.push_back(CandidateSlotDesc{ "querier", true });
        const JsonValue* slots = data.Find("slots");
        if (slots == nullptr)
            return true;
        bool ok = true;
        for (const JsonValue& slot : slots->AsArray())
        {
            const std::string& name = slot.Find("name")->AsString();
            const bool duplicate = std::ranges::any_of(out.Slots, [&](const CandidateSlotDesc& existing) {
                return existing.Name == name;
            });
            if (duplicate)
            {
                errors.push_back(std::format("slot '{}' is declared twice", name));
                ok = false;
                continue;
            }
            const JsonValue* required = slot.Find("required");
            out.Slots.push_back(CandidateSlotDesc{ name, required == nullptr || required->AsBool() });
        }
        if (out.Slots.size() > 255)
        {
            errors.push_back("an evaluation declares at most 254 slots");
            ok = false;
        }
        return ok;
    }

    bool CompileGenerators(const JsonValue& data,
                           const CandidateCatalogs& catalogs,
                           const CandidatePrepareContext& context,
                           CandidateEvaluationDesc& out,
                           std::vector<std::string>& errors)
    {
        const JsonValue* generators = data.Find("generators");
        if (generators == nullptr || generators->AsArray().empty())
        {
            errors.push_back("an evaluation needs at least one generator");
            return false;
        }
        bool ok = true;
        for (std::size_t index = 0; index < generators->AsArray().size(); ++index)
        {
            const JsonValue& step = generators->AsArray()[index];
            const std::string& name = step.Find("generator")->AsString();
            const std::string subject = std::format("generator {} '{}'", index, name);
            const CandidateGeneratorHandle handle = catalogs.Generators().Resolve(name);
            if (!handle.IsValid())
            {
                errors.push_back(std::format("{}: not a generator this host offers", subject));
                ok = false;
                continue;
            }
            const CandidateGeneratorDefinition& definition = *catalogs.Generators().Get(handle.Slot);
            const JsonValue& arguments = ArgumentsOf(step);
            if (!CheckArguments(arguments, definition.Arguments, false, subject, errors))
            {
                ok = false;
                continue;
            }
            const std::size_t before = errors.size();
            CandidatePrepared prepared = definition.Prepare(arguments, context, subject, errors);
            if (errors.size() != before)
            {
                ok = false;
                continue;
            }
            out.Generators.push_back(CandidateGeneratorStep{ name, handle, std::move(prepared.State) });
        }
        return ok;
    }

    bool CompileRequire(const JsonValue& require, CandidateCriterion& criterion, std::string_view subject,
                        std::vector<std::string>& errors)
    {
        if (const JsonValue* expect = require.Find("expect"))
        {
            criterion.Min = criterion.Max = expect->AsBool() ? 1.0f : 0.0f;
            return true;
        }
        criterion.Min = static_cast<float>(require.NumberOr("min", -INFINITY));
        criterion.Max = static_cast<float>(require.NumberOr("max", INFINITY));
        if (criterion.Min > criterion.Max)
        {
            errors.push_back(std::format("{}: require.min exceeds require.max", subject));
            return false;
        }
        return true;
    }

    bool CompileScore(const JsonValue& score, CandidateCriterion& criterion, std::string_view subject,
                      std::vector<std::string>& errors)
    {
        criterion.Weight = static_cast<float>(score.NumberOr("weight", 1.0));
        if (!(criterion.Weight > 0.0f) || !std::isfinite(criterion.Weight))
        {
            errors.push_back(std::format("{}: a score weight is positive and finite", subject));
            return false;
        }
        if (const JsonValue* curve = score.Find("curve"))
        {
            std::string problem;
            criterion.Curve = ReadResponseCurve(*curve, problem);
            if (!criterion.Curve)
            {
                errors.push_back(std::format("{}: {}", subject, problem));
                return false;
            }
        }
        else if (criterion.Value == CandidateValueKind::Scalar)
        {
            errors.push_back(std::format("{}: scoring this measure needs a curve", subject));
            return false;
        }
        return true;
    }

    bool CompileCriteria(const JsonValue& data,
                         const CandidateCatalogs& catalogs,
                         const CandidatePrepareContext& context,
                         CandidateEvaluationDesc& out,
                         std::vector<std::string>& errors)
    {
        const JsonValue* criteria = data.Find("criteria");
        if (criteria == nullptr)
            return true;
        bool ok = true;
        for (std::size_t index = 0; index < criteria->AsArray().size(); ++index)
        {
            const JsonValue& step = criteria->AsArray()[index];
            const std::string& name = step.Find("measure")->AsString();
            const std::string subject = std::format("criterion {} '{}'", index, name);
            const CandidateMeasureHandle handle = catalogs.Measures().Resolve(name);
            if (!handle.IsValid())
            {
                errors.push_back(std::format("{}: not a measure this host offers", subject));
                ok = false;
                continue;
            }
            const CandidateMeasureDefinition& definition = *catalogs.Measures().Get(handle.Slot);
            const JsonValue& arguments = ArgumentsOf(step);
            if (!CheckArguments(arguments, definition.Arguments, definition.OpenArguments, subject, errors))
            {
                ok = false;
                continue;
            }

            CandidateCriterion criterion;
            criterion.Operation = name;
            criterion.Handle = handle;
            criterion.Cost = definition.Cost;
            criterion.AppliesTo = definition.AppliesTo;
            criterion.Value = definition.Value;
            criterion.AuthoredIndex = static_cast<std::uint32_t>(index);

            const JsonValue* require = step.Find("require");
            const JsonValue* score = step.Find("score");
            if ((require == nullptr) == (score == nullptr))
            {
                errors.push_back(std::format("{}: exactly one of 'require' and 'score'", subject));
                ok = false;
                continue;
            }
            criterion.Mode = require != nullptr ? CandidateCriterionMode::Require : CandidateCriterionMode::Score;
            const bool modeOk = require != nullptr ? CompileRequire(*require, criterion, subject, errors)
                                                   : CompileScore(*score, criterion, subject, errors);
            if (!modeOk)
            {
                ok = false;
                continue;
            }
            if (const JsonValue* unmeasured = step.Find("unmeasured"))
                criterion.UnmeasuredScore = static_cast<float>(unmeasured->NumberOr("score", 0.0));

            const std::size_t before = errors.size();
            CandidatePrepared prepared = definition.Prepare(arguments, context, subject, errors);
            if (errors.size() != before)
            {
                ok = false;
                continue;
            }
            criterion.Prepared = std::move(prepared.State);
            if (prepared.Cost)
                criterion.Cost = *prepared.Cost;
            out.Criteria.push_back(std::move(criterion));
        }
        std::ranges::stable_sort(out.Criteria, {}, CriterionRank);
        return ok;
    }

    bool CompileSelection(const JsonValue& data, CandidateEvaluationDesc& out, std::vector<std::string>& errors)
    {
        const JsonValue* selection = data.Find("selection");
        if (selection == nullptr)
            return true;
        const std::string& mode = selection->Find("mode")->AsString();
        const auto* named = std::ranges::find(kSelectionNames, mode, &SelectionName::Name);
        out.Selection.Mode = named->Mode;
        out.Selection.Count = out.Selection.Mode == CandidateSelectionMode::TopN
            ? CountOr(*selection, "count", 1)
            : 1;
        out.Selection.BandWidth = static_cast<float>(selection->NumberOr("band_width", 0.0));
        if (out.Selection.Count == 0)
        {
            errors.push_back("selection.count is at least 1");
            return false;
        }
        return true;
    }

    void CompileBudgets(const JsonValue& data, CandidateEvaluationDesc& out)
    {
        const JsonValue* limits = data.Find("limits");
        if (limits == nullptr)
            return;
        CandidateBudgets& budgets = out.Budgets;
        budgets.Candidates = CountOr(*limits, "candidates", budgets.Candidates);
        budgets.ExactNavSearches = CountOr(*limits, "exact_nav_searches", budgets.ExactNavSearches);
        budgets.Raycasts = CountOr(*limits, "raycasts", budgets.Raycasts);
        budgets.ReachableRegions = CountOr(*limits, "reachable_regions", budgets.ReachableRegions);
    }

    [[nodiscard]] DataFieldSchema OpenRecord(std::string key, std::string displayName)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Record, std::move(key), std::move(displayName));
        field.Required = false;
        return field;
    }

    [[nodiscard]] DataFieldSchema Optional(DataFieldKind kind, std::string key, std::string displayName)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(displayName));
        field.Required = false;
        return field;
    }

    [[nodiscard]] DataFieldSchema ArrayOf(std::string key, std::string displayName, DataFieldSchema element)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Array, std::move(key), std::move(displayName));
        field.Required = false;
        field.Editor.Widget = "cards";
        field.Children.push_back(std::move(element));
        return field;
    }
}

bool CompileCandidateEvaluationDesc(const JsonValue& data,
                                    const CandidateCatalogs& catalogs,
                                    CandidateEvaluationDesc& out,
                                    std::vector<std::string>& errors)
{
    std::vector<DataValidationError> problems;
    if (!ValidateDataAgainstSchema(data, MakeCandidateEvaluationSchema(), problems))
    {
        errors.push_back(FormatDataValidationErrors(problems));
        return false;
    }

    out = CandidateEvaluationDesc{};
    const JsonValue* name = data.Find("name");
    out.Name = name != nullptr ? name->AsString() : std::string{};

    bool ok = CompileSlots(data, out, errors);
    std::vector<std::string> slotNames;
    slotNames.reserve(out.Slots.size());
    for (const CandidateSlotDesc& slot : out.Slots)
        slotNames.push_back(slot.Name);
    const CandidatePrepareContext context{ slotNames };

    ok = CompileGenerators(data, catalogs, context, out, errors) && ok;
    ok = CompileCriteria(data, catalogs, context, out, errors) && ok;
    ok = CompileSelection(data, out, errors) && ok;
    CompileBudgets(data, out);
    return ok;
}

DataSchema MakeCandidateEvaluationSchema()
{
    DataFieldSchema slot = MakeDataField(DataFieldKind::Record, "slot", "Slot");
    slot.Children.push_back(MakeDataField(DataFieldKind::String, "name", "Name",
                                          "What measures and generators call this input."));
    DataFieldSchema required = Optional(DataFieldKind::Bool, "required", "Required");
    required.Default = true;
    slot.Children.push_back(std::move(required));

    DataFieldSchema generator = MakeDataField(DataFieldKind::Record, "generator", "Generator");
    generator.Children.push_back(MakeDataField(DataFieldKind::String, "generator", "Generator",
                                               "A candidates.generator.* operation."));
    generator.Children.push_back(OpenRecord("arguments", "Arguments"));

    DataFieldSchema require = OpenRecord("require", "Require");
    require.Children.push_back(Optional(DataFieldKind::Float, "min", "Minimum"));
    require.Children.push_back(Optional(DataFieldKind::Float, "max", "Maximum"));
    require.Children.push_back(Optional(DataFieldKind::Bool, "expect", "Expect"));

    DataFieldSchema score = OpenRecord("score", "Score");
    DataFieldSchema curve = MakeResponseCurveField("curve", "Curve");
    curve.Required = false;
    score.Children.push_back(std::move(curve));
    DataFieldSchema weight = Optional(DataFieldKind::Float, "weight", "Weight");
    weight.Default = 1.0;
    score.Children.push_back(std::move(weight));

    DataFieldSchema unmeasured = OpenRecord("unmeasured", "When unmeasured");
    unmeasured.Summary = "Score unmeasured candidates instead of rejecting them.";
    DataFieldSchema unmeasuredScore = MakeDataField(DataFieldKind::Float, "score", "Score");
    unmeasuredScore.Numeric.Minimum = 0.0;
    unmeasuredScore.Numeric.Maximum = 1.0;
    unmeasured.Children.push_back(std::move(unmeasuredScore));

    DataFieldSchema criterion = MakeDataField(DataFieldKind::Record, "criterion", "Criterion");
    criterion.Children.push_back(MakeDataField(DataFieldKind::String, "measure", "Measure",
                                               "A candidates.measure.* operation."));
    criterion.Children.push_back(OpenRecord("arguments", "Arguments"));
    criterion.Children.push_back(std::move(require));
    criterion.Children.push_back(std::move(score));
    criterion.Children.push_back(std::move(unmeasured));

    DataFieldSchema mode = MakeDataField(DataFieldKind::Enum, "mode", "Mode");
    for (const SelectionName& named : kSelectionNames)
        mode.EnumChoices.push_back(DataEnumChoice{ std::string(named.Name), std::string(named.Name), {} });
    DataFieldSchema selection = OpenRecord("selection", "Selection");
    selection.Children.push_back(std::move(mode));
    DataFieldSchema count = Optional(DataFieldKind::Int, "count", "Count");
    count.Numeric.Minimum = 1.0;
    selection.Children.push_back(std::move(count));
    DataFieldSchema bandWidth = Optional(DataFieldKind::Float, "band_width", "Band width");
    bandWidth.Numeric.Minimum = 0.0;
    selection.Children.push_back(std::move(bandWidth));

    DataFieldSchema limits = OpenRecord("limits", "Limits");
    for (const char* key : { "candidates", "exact_nav_searches", "raycasts", "reachable_regions" })
    {
        DataFieldSchema limit = Optional(DataFieldKind::Int, key, key);
        limit.Numeric.Minimum = 0.0;
        limits.Children.push_back(std::move(limit));
    }

    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    root.Children.push_back(Optional(DataFieldKind::String, "name", "Name"));
    root.Children.push_back(ArrayOf("slots", "Slots", std::move(slot)));
    DataFieldSchema generators = ArrayOf("generators", "Generators", std::move(generator));
    generators.Required = true;
    root.Children.push_back(std::move(generators));
    root.Children.push_back(ArrayOf("criteria", "Criteria", std::move(criterion)));
    root.Children.push_back(std::move(selection));
    root.Children.push_back(std::move(limits));

    DataSchema schema;
    schema.TypeName = "candidates.evaluation";
    schema.DisplayName = "Candidate evaluation";
    schema.Description = "Which places or entities qualify for a question, and how they rank.";
    schema.Root = std::move(root);
    schema.AllowUnknownFields = true;
    return schema;
}
