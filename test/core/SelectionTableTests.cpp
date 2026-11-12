// A selection table compiles without a World, binds its queries and interrupt
// against one, and picks an option by weighted curves, held by commitment and
// margin, with a seeded band pick that is a pure function of its inputs.

#include <authored/AuthoredQueryDispatcher.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/selection/SelectionEvaluator.h>
#include <logic/selection/SelectionTable.h>
#include <logic/selection/SelectionTableDesc.h>

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    // Answers test.value(subject) from a per-entity table; Unavailable for an
    // entity it has no value for.
    struct Values
    {
        std::map<EntityId, double> Of;

        static AuthoredQueryStatus Evaluate(const Values& self, std::span<const AuthoredValue> arguments,
                                            AuthoredValue& result)
        {
            EntityId subject;
            if (!arguments[0].TryGetEntity(subject))
                return AuthoredQueryStatus::InvalidArguments;
            const auto found = self.Of.find(subject);
            if (found == self.Of.end())
                return AuthoredQueryStatus::Unavailable;
            result = AuthoredValue::Float(found->second);
            return AuthoredQueryStatus::Value;
        }
    };

    AuthoredQueryDefinition Query(std::string name, DataFieldKind result)
    {
        AuthoredQueryDefinition definition;
        definition.Name = std::move(name);
        DataFieldSchema subject;
        subject.Key = "subject";
        subject.Kind = DataFieldKind::Entity;
        definition.Arguments.Children.push_back(std::move(subject));
        definition.Result.Kind = result;
        return definition;
    }

    struct SelectionHarness
    {
        World Entities;
        AuthoredQueryRegistry Registry;
        std::optional<AuthoredQueryDispatcher> Dispatcher;
        std::vector<AuthoredQueryBindingToken> Tokens;
        Values Answers;
        SelectionScratch Scratch;
        EntityId Querier;
        EntityId Threat;

        SelectionHarness()
        {
            Entities.RegisterComponent<GameplayTagContainer>();
            Entities.AddResource<GameplayTagRegistry>();
            (void)Entities.GetResource<GameplayTagRegistry>().RegisterTag("state.hit");
            AuthoredQueryRegistrationScope scope(Registry, "test");
            (void)scope.Declare(Query("test.value", DataFieldKind::Float));
            (void)scope.Declare(Query("test.name", DataFieldKind::String));
            EXPECT_TRUE(scope.Commit());
            Dispatcher.emplace(Registry);
            Tokens.push_back(Dispatcher->Bind<&Values::Evaluate>(Registry.Find("test.value"), Answers));
            Querier = Entities.CreateEntity();
            Threat = Entities.CreateEntity();
        }

        static JsonValue Json(std::string_view text)
        {
            std::optional<JsonValue> parsed = JsonParse(text);
            EXPECT_TRUE(parsed.has_value()) << text;
            return parsed.value_or(JsonValue(JsonValue::Object{}));
        }

        std::optional<SelectionTable> Bind(std::string_view text, std::vector<std::string>* errorsOut = nullptr)
        {
            SelectionTableDesc desc;
            std::vector<std::string> errors;
            if (!CompileSelectionTableDesc(Json(text), desc, errors))
            {
                if (errorsOut != nullptr)
                    *errorsOut = errors;
                return std::nullopt;
            }
            SelectionTable table;
            if (!BindSelectionTable(desc, &Registry, &Entities.GetResource<GameplayTagRegistry>(), table, errors))
            {
                if (errorsOut != nullptr)
                    *errorsOut = errors;
                return std::nullopt;
            }
            return table;
        }

        SelectionResult Choose(const SelectionTable& table, std::vector<float> inputs,
                               std::optional<std::uint32_t> current = std::nullopt, float currentSeconds = 0.0f,
                               std::uint64_t seed = 0, SelectionTrace* trace = nullptr)
        {
            const EntityId entities[] = { Querier, Threat };
            const SelectionEvaluator evaluator(Entities, &*Dispatcher);
            return evaluator.Evaluate(table,
                                      SelectionContext{ .Entities = entities,
                                                        .Inputs = inputs,
                                                        .Current = current,
                                                        .CurrentSeconds = currentSeconds,
                                                        .Seed = seed },
                                      Scratch, trace);
        }
    };

    constexpr std::string_view kDistanceTable = R"({
        "name": "distance",
        "entities": ["threat"],
        "inputs": ["distance"],
        "switch_margin": 0.2,
        "options": [
            { "name": "close", "commit_seconds": 1.0, "considerations": [
                { "input": "distance", "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 10 } } } ] },
            { "name": "far", "considerations": [
                { "input": "distance", "score": { "curve": { "shape": "linear", "low": 0, "high": 10 } } } ] }
        ]
    })";
}

TEST(SelectionTable, CompileRejectsWhatItCannotResolve)
{
    SelectionHarness harness;
    std::vector<std::string> errors;
    EXPECT_FALSE(harness.Bind(R"({ "options": [ { "name": "a", "considerations": [
        { "input": "missing", "score": { "curve": { "shape": "linear", "low": 0, "high": 1 } } } ] } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("'missing' is not an input"), std::string::npos);

    errors.clear();
    EXPECT_FALSE(harness.Bind(R"({ "inputs": ["x"], "options": [ { "name": "a", "considerations": [
        { "input": "x", "score": {} } ] } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("needs a curve"), std::string::npos);

    errors.clear();
    EXPECT_FALSE(harness.Bind(R"({ "options": [ { "name": "a" }, { "name": "a" } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("used twice"), std::string::npos);

    errors.clear();
    EXPECT_FALSE(harness.Bind(R"({ "entities": ["threat"], "options": [ { "name": "a", "considerations": [
        { "query": "test.value", "entities": { "subject": "nobody" }, "require": { "min": 0 } } ] } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("'nobody' is not an entity"), std::string::npos);
}

TEST(SelectionTable, BindRejectsUnknownAndUnscorableQueries)
{
    SelectionHarness harness;
    std::vector<std::string> errors;
    EXPECT_FALSE(harness.Bind(R"({ "options": [ { "name": "a", "considerations": [
        { "query": "test.nothing", "require": { "min": 0 } } ] } ] })", &errors));
    EXPECT_FALSE(errors.empty());

    errors.clear();
    EXPECT_FALSE(harness.Bind(R"({ "entities": ["threat"], "options": [ { "name": "a", "considerations": [
        { "query": "test.name", "entities": { "subject": "threat" }, "require": { "min": 0 } } ] } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("no curve can score"), std::string::npos);

    errors.clear();
    EXPECT_FALSE(harness.Bind(R"({ "entities": ["threat"], "options": [ { "name": "a", "considerations": [
        { "query": "test.value", "entities": { "subject": "threat" }, "score": {} } ] } ] })", &errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("needs a curve"), std::string::npos);
}

TEST(SelectionTable, ChoosesTheBestWeightedOption)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(kDistanceTable);
    ASSERT_TRUE(table);
    EXPECT_EQ(harness.Choose(*table, { 2.0f }).Option, 0u);
    const SelectionResult far = harness.Choose(*table, { 9.0f });
    EXPECT_EQ(far.Status, SelectionStatus::Success);
    EXPECT_EQ(far.Option, 1u);
    EXPECT_NEAR(far.Score, 0.9f, 1e-6f);
}

TEST(SelectionTable, ARequirementDisqualifiesAndTheTraceSaysWhich)
{
    SelectionHarness harness;
    harness.Answers.Of[harness.Threat] = 3.0;
    const std::optional<SelectionTable> table = harness.Bind(R"({
        "entities": ["threat"],
        "options": [
            { "name": "needs_five", "considerations": [
                { "query": "test.value", "entities": { "subject": "threat" }, "require": { "min": 5 } } ] },
            { "name": "fallback" }
        ]
    })");
    ASSERT_TRUE(table);
    SelectionTrace trace;
    const SelectionResult result = harness.Choose(*table, {}, std::nullopt, 0.0f, 0, &trace);
    EXPECT_EQ(result.Option, 1u);
    ASSERT_EQ(trace.Options.size(), 2u);
    EXPECT_FALSE(trace.Options[0].Qualified);
    EXPECT_EQ(trace.Options[0].RejectedBy, 0u);
    EXPECT_FLOAT_EQ(trace.Values[trace.Options[0].FirstValue].Value, 3.0f);
    EXPECT_EQ(DescribeSelectionTrace(trace), "  option 0: disqualified by consideration 0\n  option 1: score 1.000\n");

    harness.Answers.Of[harness.Threat] = 7.0;
    EXPECT_EQ(harness.Choose(*table, {}).Option, 0u);
}

TEST(SelectionTable, UnmeasuredValuesDisqualifyUnlessScored)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> strict = harness.Bind(R"({
        "entities": ["threat"],
        "options": [ { "name": "a", "considerations": [
            { "query": "test.value", "entities": { "subject": "threat" },
              "score": { "curve": { "shape": "linear", "low": 0, "high": 10 } } } ] } ]
    })");
    ASSERT_TRUE(strict);
    EXPECT_EQ(harness.Choose(*strict, {}).Status, SelectionStatus::NoneQualified);

    const std::optional<SelectionTable> lenient = harness.Bind(R"({
        "entities": ["threat"],
        "options": [ { "name": "a", "considerations": [
            { "query": "test.value", "entities": { "subject": "threat" }, "unmeasured": { "score": 0.25 },
              "score": { "curve": { "shape": "linear", "low": 0, "high": 10 } } } ] } ]
    })");
    ASSERT_TRUE(lenient);
    const SelectionResult result = harness.Choose(*lenient, {});
    EXPECT_EQ(result.Status, SelectionStatus::Success);
    EXPECT_FLOAT_EQ(result.Score, 0.25f);
}

TEST(SelectionTable, CommitmentHoldsUntilItRunsOutOrIsInterrupted)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(R"({
        "inputs": ["distance"],
        "interrupt": { "any": ["state.hit"] },
        "options": [
            { "name": "close", "commit_seconds": 1.0, "considerations": [
                { "input": "distance", "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 10 } } } ] },
            { "name": "far", "considerations": [
                { "input": "distance", "score": { "curve": { "shape": "linear", "low": 0, "high": 10 } } } ] }
        ]
    })");
    ASSERT_TRUE(table);

    const SelectionResult held = harness.Choose(*table, { 9.0f }, 0u, 0.5f);
    EXPECT_EQ(held.Option, 0u);
    EXPECT_EQ(held.Decision, SelectionDecision::Committed);
    EXPECT_EQ(harness.Choose(*table, { 9.0f }, 0u, 1.0f).Option, 1u);

    GameplayTagContainer tags;
    (void)tags.Grant(harness.Entities.GetResource<GameplayTagRegistry>().FindTag("state.hit"));
    harness.Entities.AddComponent<GameplayTagContainer>(harness.Querier, tags);
    const SelectionResult interrupted = harness.Choose(*table, { 9.0f }, 0u, 0.5f);
    EXPECT_TRUE(interrupted.Interrupted);
    EXPECT_EQ(interrupted.Option, 1u);
}

TEST(SelectionTable, AnotherOptionMustBeatTheCurrentByTheMargin)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(kDistanceTable);
    ASSERT_TRUE(table);
    const SelectionResult held = harness.Choose(*table, { 5.5f }, 0u, 5.0f);
    EXPECT_EQ(held.Option, 0u);
    EXPECT_EQ(held.Decision, SelectionDecision::WithinMargin);
    EXPECT_EQ(harness.Choose(*table, { 6.5f }, 0u, 5.0f).Option, 1u);
}

TEST(SelectionTable, BandPickIsSeededAndStaysInTheBand)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(R"({
        "inputs": ["a", "b", "c", "d"],
        "selection": { "mode": "pick_from_band", "band_width": 0.1 },
        "options": [
            { "name": "a", "considerations": [ { "input": "a", "score": { "curve": { "shape": "linear", "low": 0, "high": 1 } } } ] },
            { "name": "b", "considerations": [ { "input": "b", "score": { "curve": { "shape": "linear", "low": 0, "high": 1 } } } ] },
            { "name": "c", "considerations": [ { "input": "c", "score": { "curve": { "shape": "linear", "low": 0, "high": 1 } } } ] },
            { "name": "d", "considerations": [ { "input": "d", "score": { "curve": { "shape": "linear", "low": 0, "high": 1 } } } ] }
        ]
    })");
    ASSERT_TRUE(table);
    std::set<std::uint32_t> picked;
    for (std::uint64_t seed = 0; seed < 64; ++seed)
    {
        const std::uint32_t option = harness.Choose(*table, { 0.95f, 0.9f, 0.88f, 0.5f }, std::nullopt, 0.0f, seed).Option;
        EXPECT_EQ(option, harness.Choose(*table, { 0.95f, 0.9f, 0.88f, 0.5f }, std::nullopt, 0.0f, seed).Option);
        picked.insert(option);
    }
    EXPECT_EQ(picked, (std::set<std::uint32_t>{ 0u, 1u, 2u }));
}

TEST(SelectionTable, AnOptionTheCallerRulesOutNeitherWinsNorHolds)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(kDistanceTable);
    ASSERT_TRUE(table);
    const EntityId entities[] = { harness.Querier, harness.Threat };
    const std::vector<float> inputs{ 2.0f };
    const SelectionEvaluator evaluator(harness.Entities, &*harness.Dispatcher);
    SelectionTrace trace;
    const SelectionResult result = evaluator.Evaluate(
        *table, SelectionContext{ .Entities = entities, .Inputs = inputs, .Current = 0u, .CurrentSeconds = 0.0f,
                                  .Unavailable = 1u },
        harness.Scratch, &trace);
    EXPECT_EQ(result.Option, 1u);
    EXPECT_EQ(result.Decision, SelectionDecision::Chosen);
    EXPECT_TRUE(trace.Options[0].Unavailable);
    EXPECT_FALSE(trace.Options[0].Qualified);
}

TEST(SelectionTable, TooSmallAContextIsMissingNotAChoice)
{
    SelectionHarness harness;
    const std::optional<SelectionTable> table = harness.Bind(kDistanceTable);
    ASSERT_TRUE(table);
    EXPECT_EQ(harness.Choose(*table, {}).Status, SelectionStatus::ContextMissing);
    EXPECT_EQ(harness.Choose(*table, { 1.0f }, 7u).Status, SelectionStatus::ContextMissing);
}
