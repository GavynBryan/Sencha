#pragma once

#include <spatial/candidates/CandidateCatalogs.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <math/ResponseCurve.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// A definition compiled with no World, shared by every World that binds it.
// Authored form: docs/spatial/candidates.md.

enum class CandidateCriterionMode : std::uint8_t
{
    // Rejects candidates whose value lies outside [Min, Max].
    Require,
    // Maps the value through Curve and weights it.
    Score,
};

enum class CandidateSelectionMode : std::uint8_t
{
    Best,
    TopN,
    AllQualified,
    PickFromBand,
};

struct CandidateSlotDesc
{
    std::string Name;
    bool Required = true;
};

struct CandidateGeneratorStep
{
    std::string Operation;
    CandidateGeneratorHandle Handle;
    std::shared_ptr<const void> Prepared;
};

struct CandidateCriterion
{
    std::string Operation;
    CandidateMeasureHandle Handle;
    std::shared_ptr<const void> Prepared;

    CandidateCostClass Cost = CandidateCostClass::Geometric;
    CandidateAppliesTo AppliesTo = CandidateAppliesTo::Any;
    CandidateValueKind Value = CandidateValueKind::Scalar;

    CandidateCriterionMode Mode = CandidateCriterionMode::Require;
    float Min = -std::numeric_limits<float>::infinity();
    float Max = std::numeric_limits<float>::infinity();
    std::optional<ResponseCurve> Curve;
    float Weight = 1.0f;

    // Unmeasured candidates are rejected unless this is set.
    std::optional<float> UnmeasuredScore;

    // Position in the authored list; scores sum in this order.
    std::uint32_t AuthoredIndex = 0;
};

struct CandidateSelection
{
    CandidateSelectionMode Mode = CandidateSelectionMode::Best;
    std::uint32_t Count = 1;
    float BandWidth = 0.0f;
};

// Counts, never time, so identical inputs stop at the same place.
struct CandidateBudgets
{
    std::uint32_t Candidates = 256;
    std::uint32_t ExactNavSearches = 16;
    std::uint32_t Raycasts = 128;
    std::uint32_t ReachableRegions = 512;
};

struct CandidateEvaluationDesc
{
    std::string Name;
    // Index 0 is the querier; authored slots follow in authored order.
    std::vector<CandidateSlotDesc> Slots;
    std::vector<CandidateGeneratorStep> Generators;
    // Run order: cost class, then Require before Score, then authored order.
    std::vector<CandidateCriterion> Criteria;
    CandidateSelection Selection;
    CandidateBudgets Budgets;
};

// Collects every problem before returning false.
[[nodiscard]] bool CompileCandidateEvaluationDesc(const JsonValue& data,
                                                  const CandidateCatalogs& catalogs,
                                                  CandidateEvaluationDesc& out,
                                                  std::vector<std::string>& errors);

// The authored envelope. Operation arguments are checked against each
// operation's own schema at compile, so the envelope admits any members there.
[[nodiscard]] DataSchema MakeCandidateEvaluationSchema();
