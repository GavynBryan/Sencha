// Generation work is bounded by the evaluation's attempt budget, whatever the
// authored radius, spacing or counts, and pathological numbers are refused
// when the evaluation compiles rather than reaching an int.

#include "CandidateHarness.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace
{
    const ZoneId kRoom{ 1 };

    NavTestGeometry Floor(float size)
    {
        NavTestGeometry geometry;
        geometry.AddFloor(0.0f, 0.0f, size, size);
        return geometry;
    }

    bool Mentions(const std::vector<std::string>& errors, std::string_view text)
    {
        return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
    }

    // Every candidate a run returns, through a buffer larger than any grid here.
    std::vector<Vec3d> ReturnedPositions(CandidateHarness& harness, const CandidateEvaluation& evaluation,
                                         CandidateRunResult& result)
    {
        CandidateResultBuffer results(256);
        result = harness.Evaluator.Evaluate(evaluation, QuerierAt(harness, kRoom, Vec3d::Zero()), harness.Scratch,
                                            results);
        std::vector<Vec3d> positions;
        for (const CandidateResultEntry& entry : results.Entries())
            positions.push_back(entry.Position);
        return positions;
    }

    std::string Generator(std::string_view name, std::string_view arguments, std::string_view limits = "{}")
    {
        return std::format(R"({{ "generators": [{{ "generator": "{}", "arguments": {} }}],
                                 "selection": {{ "mode": "all_qualified" }}, "limits": {} }})",
                           name, arguments, limits);
    }
}

TEST(CandidateGenerationBounds, PathologicalNumbersAreRefusedWhenTheEvaluationCompiles)
{
    CandidateHarness harness;
    EXPECT_TRUE(Mentions(harness.CompileErrors(Generator("candidates.generator.grid", R"({ "radius": 1e6, "spacing": 0.001 })")),
                         "steps"));
    EXPECT_TRUE(Mentions(harness.CompileErrors(Generator("candidates.generator.grid", R"({ "radius": 1e39, "spacing": 1 })")),
                         "radius is a finite number"));
    EXPECT_TRUE(Mentions(harness.CompileErrors(Generator("candidates.generator.reachable",
                                                         R"({ "radius": 1e6, "max_cost": 10, "spacing": 0.001 })")),
                         "steps"));
    EXPECT_TRUE(Mentions(harness.CompileErrors(Generator("candidates.generator.ring",
                                                         R"({ "outer_radius": 5, "points_per_ring": 1e12 })")),
                         "points_per_ring is a whole number"));
    EXPECT_TRUE(Mentions(harness.CompileErrors(Generator("candidates.generator.ring",
                                                         R"({ "outer_radius": 1e39 })")),
                         "outer_radius is a finite number"));
}

TEST(CandidateGenerationBounds, FailedProjectionsSpendAttemptsAndStopGeneration)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(10.0f));
    // Far off the floor, so every sample fails to project.
    const std::array far{ CandidatePoint{ .Position = Vec3d(500, 0, 500) } };
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "far" }],
        "generators": [{ "generator": "candidates.generator.grid",
                         "arguments": { "center": "far", "radius": 60, "spacing": 0.5 } }],
        "selection": { "mode": "all_qualified" },
        "limits": { "generation_attempts": 100 }
    })");
    const std::array slots{ CandidateSlotBinding{ 1, far } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(5, 0, 5));
    context.Slots = slots;
    CandidateTrace trace(CandidateTraceLevel::Summary, 0);

    const CandidateRunResult result = harness.Run(evaluation, context, &trace);
    EXPECT_EQ(result.Generated, 0u);
    EXPECT_EQ(trace.DroppedAtGeneration, 100u);
    EXPECT_TRUE(result.Flags.BudgetExhausted);
    EXPECT_TRUE(result.Flags.GenerationTruncated);
}

TEST(CandidateGenerationBounds, ReachableSamplesOffTheReachableSetSpendAttemptsToo)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(10.0f));
    const std::array far{ CandidatePoint{ .Position = Vec3d(500, 0, 500) } };
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "far" }],
        "generators": [{ "generator": "candidates.generator.reachable",
                         "arguments": { "center": "far", "radius": 60, "max_cost": 100, "spacing": 0.5 } }],
        "selection": { "mode": "all_qualified" },
        "limits": { "generation_attempts": 64 }
    })");
    const std::array slots{ CandidateSlotBinding{ 1, far } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(5, 0, 5));
    context.Slots = slots;
    CandidateTrace trace(CandidateTraceLevel::Summary, 0);

    const CandidateRunResult result = harness.Run(evaluation, context, &trace);
    EXPECT_EQ(result.Generated, 0u);
    EXPECT_EQ(trace.DroppedAtGeneration, 64u);
    EXPECT_TRUE(result.Flags.GenerationTruncated);
}

TEST(CandidateGenerationBounds, ExhaustionKeepsTheFirstCandidatesInGenerationOrder)
{
    CandidateHarness harness;
    harness.AttachBareZone(kRoom);
    const auto run = [&](std::string_view generator, std::string_view arguments, std::uint32_t attempts) {
        const CandidateEvaluation evaluation =
            harness.Compile(Generator(generator, arguments, std::format(R"({{ "generation_attempts": {} }})", attempts)));
        CandidateRunResult result;
        std::vector<Vec3d> positions = ReturnedPositions(harness, evaluation, result);
        return std::pair{ result, positions };
    };

    for (const auto& [generator, arguments] :
         { std::pair{ "candidates.generator.grid", R"({ "radius": 5, "spacing": 1, "project": false })" },
           std::pair{ "candidates.generator.ring", R"({ "rings": 3, "points_per_ring": 12, "inner_radius": 1, "outer_radius": 4, "project": false })" } })
    {
        const auto [whole, all] = run(generator, arguments, 4096);
        ASSERT_FALSE(whole.Flags.GenerationTruncated) << generator;
        ASSERT_FALSE(whole.Flags.OutputTruncated) << generator;
        ASSERT_GT(all.size(), 10u) << generator;

        const auto [cut, first] = run(generator, arguments, 10);
        EXPECT_TRUE(cut.Flags.GenerationTruncated) << generator;
        EXPECT_TRUE(cut.Flags.BudgetExhausted) << generator;
        ASSERT_EQ(first.size(), 10u) << generator;
        EXPECT_TRUE(std::equal(first.begin(), first.end(), all.begin())) << generator;
    }
}

TEST(CandidateGenerationBounds, TheGridVisitsExactlyThePointsWithinItsRadiusRowByRow)
{
    CandidateHarness harness;
    harness.AttachBareZone(kRoom);
    for (const auto& [radius, spacing] : { std::pair{ 5.0f, 1.0f }, std::pair{ 4.3f, 0.7f }, std::pair{ 7.0f, 2.5f } })
    {
        const CandidateEvaluation evaluation = harness.Compile(Generator(
            "candidates.generator.grid",
            std::format(R"({{ "radius": {}, "spacing": {}, "project": false }})", radius, spacing),
            R"({ "candidates": 256 })"));
        CandidateRunResult result;
        const std::vector<Vec3d> returned = ReturnedPositions(harness, evaluation, result);
        ASSERT_EQ(result.Status, CandidateRunStatus::Success);
        EXPECT_FALSE(result.Flags.OutputTruncated);

        std::vector<Vec3d> expected;
        const int steps = static_cast<int>(std::floor(radius / spacing));
        for (int z = -steps; z <= steps; ++z)
            for (int x = -steps; x <= steps; ++x)
            {
                const float dx = static_cast<float>(x) * spacing;
                const float dz = static_cast<float>(z) * spacing;
                if (dx * dx + dz * dz <= radius * radius)
                    expected.emplace_back(dx, 0.0f, dz);
            }
        EXPECT_EQ(returned, expected) << radius << " / " << spacing;
    }
}

TEST(CandidateGenerationBounds, AnEvaluationHoldsAtMostTheGeneratorsAByteCanName)
{
    const auto withGenerators = [](std::uint32_t count) {
        std::string json = R"({ "generators": [)";
        for (std::uint32_t index = 0; index < count; ++index)
            json += std::string(index == 0 ? "" : ",")
                + R"({ "generator": "candidates.generator.points", "arguments": { "slot": "querier" } })";
        return json + "] }";
    };

    CandidateHarness harness;
    (void)harness.CompileDesc(withGenerators(kMaxCandidateGenerators));
    EXPECT_TRUE(Mentions(harness.CompileErrors(withGenerators(kMaxCandidateGenerators + 1)), "at most 255 generators"));
}
