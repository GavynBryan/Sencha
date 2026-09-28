// Compiling a candidate evaluation: names against the host's catalogs,
// arguments against each operation's schema, and criteria into run order.

#include "CandidateHarness.h"

#include <algorithm>

namespace
{
    bool Mentions(const std::vector<std::string>& errors, std::string_view text)
    {
        return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
    }
}

TEST(CandidateCompile, TheHostOffersTheEngineOperations)
{
    const CandidateCatalogs catalogs;
    for (const char* name : { "candidates.generator.points", "candidates.generator.entities", "candidates.generator.ring",
                              "candidates.generator.grid", "candidates.generator.reachable" })
        EXPECT_TRUE(catalogs.Generators().Find(name).IsValid()) << name;
    for (const char* name : { "candidates.measure.distance", "candidates.measure.height", "candidates.measure.facing",
                              "candidates.measure.entity_tags", "candidates.measure.reachable",
                              "candidates.measure.travel_cost", "candidates.measure.visibility",
                              "candidates.measure.route_visibility", "candidates.measure.authored_query" })
        EXPECT_TRUE(catalogs.Measures().Find(name).IsValid()) << name;
}

TEST(CandidateCompile, CriteriaRunCheapestFirstRequireBeforeScore)
{
    CandidateHarness harness;
    const CandidateEvaluationDesc desc = harness.CompileDesc(R"({
        "slots": [{ "name": "target" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "target" } }],
        "criteria": [
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "target" }, "require": { "expect": true } },
            { "measure": "candidates.measure.travel_cost", "arguments": { "fidelity": "exact" },
              "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 30 } } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "target" },
              "score": { "curve": { "shape": "linear", "low": 0, "high": 10 } } },
            { "measure": "candidates.measure.travel_cost", "require": { "max": 40 } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "target" }, "require": { "min": 2 } }
        ]
    })");

    ASSERT_EQ(desc.Criteria.size(), 5u);
    const std::uint32_t expected[] = { 4, 2, 3, 0, 1 };
    for (std::size_t index = 0; index < desc.Criteria.size(); ++index)
        EXPECT_EQ(desc.Criteria[index].AuthoredIndex, expected[index]) << index;
    EXPECT_EQ(desc.Criteria[4].Cost, CandidateCostClass::NavigationExact);
    EXPECT_EQ(desc.Slots.size(), 2u);
    EXPECT_EQ(desc.Slots[0].Name, "querier");
}

TEST(CandidateCompile, ReportsEveryProblemWithoutAWorld)
{
    CandidateHarness harness;
    const std::vector<std::string> errors = harness.CompileErrors(R"({
        "generators": [
            { "generator": "candidates.generator.nowhere" },
            { "generator": "candidates.generator.points", "arguments": { "slot": "target" } }
        ],
        "criteria": [
            { "measure": "candidates.measure.distance", "arguments": { "slot": "querier", "bogus": 1 }, "require": {} },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "querier" } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "querier" }, "score": {} },
            { "measure": "candidates.measure.height", "arguments": { "slot": "querier" }, "require": { "min": 3, "max": 1 } }
        ]
    })");

    EXPECT_TRUE(Mentions(errors, "not a generator this host offers"));
    EXPECT_TRUE(Mentions(errors, "'target' names no slot"));
    EXPECT_TRUE(Mentions(errors, "unknown field"));
    EXPECT_TRUE(Mentions(errors, "exactly one of 'require' and 'score'"));
    EXPECT_TRUE(Mentions(errors, "needs a curve"));
    EXPECT_TRUE(Mentions(errors, "require.min exceeds require.max"));
}

TEST(CandidateCompile, BindResolvesTagsAgainstTheWorld)
{
    CandidateHarness harness;
    (void)harness.Tags.RegisterTag("pickup.health");
    const CandidateEvaluationDesc desc = harness.CompileDesc(R"({
        "generators": [{ "generator": "candidates.generator.entities", "arguments": { "tags": { "all": ["pickup.health"] } } }],
        "criteria": [{ "measure": "candidates.measure.entity_tags", "arguments": { "tags": { "none": ["pickup.spent"] } },
                       "require": { "expect": true } }]
    })");

    CandidateEvaluation evaluation;
    std::vector<std::string> errors;
    EXPECT_FALSE(BindCandidateEvaluation(desc, harness.Catalogs, { &harness.Tags, nullptr }, evaluation, errors));
    EXPECT_TRUE(Mentions(errors, "'pickup.spent' is not a registered gameplay tag"));

    errors.clear();
    (void)harness.Tags.RegisterTag("pickup.spent");
    EXPECT_TRUE(BindCandidateEvaluation(desc, harness.Catalogs, { &harness.Tags, nullptr }, evaluation, errors))
        << CandidateHarness::Joined(errors);
    EXPECT_EQ(evaluation.Criteria().size(), 1u);
}

TEST(CandidateCompile, SlotsResolveByNameForBinding)
{
    CandidateHarness harness;
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "target" }, { "name": "allies", "required": false }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "target" } }]
    })");
    EXPECT_EQ(evaluation.FindSlot("querier"), std::optional<std::uint8_t>(0));
    EXPECT_EQ(evaluation.FindSlot("target"), std::optional<std::uint8_t>(1));
    EXPECT_EQ(evaluation.FindSlot("allies"), std::optional<std::uint8_t>(2));
    EXPECT_FALSE(evaluation.FindSlot("enemies").has_value());
}
