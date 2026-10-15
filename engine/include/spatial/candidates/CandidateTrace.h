#pragma once

#include <spatial/candidates/CandidateEvaluationDesc.h>
#include <spatial/candidates/CandidateTypes.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// What one run asked, considered, measured and chose, as plain owned data for
// tools to keep, diff or send. Filled only when passed to a run, and only by
// copying what the run already computed; tracing never changes a result.

enum class CandidateTraceLevel : std::uint8_t
{
    // Header and per-criterion counts.
    Summary,
    // Also every candidate, with every value.
    Full,
};

enum class CandidateTraceOutcome : std::uint8_t
{
    Selected,
    RankedNotReturned,
    Rejected,
};

// Measure-specific evidence, e.g. what blocked a sight line. The meaning of
// Code is each measure's own, listed in docs/spatial/candidates.md.
struct CandidateDetail
{
    Vec3d Point = Vec3d::Zero();
    EntityId Entity{};
    std::uint32_t Code = 0;
};

struct CandidateTraceCriterion
{
    std::string Measure;
    CandidateCriterionMode Mode = CandidateCriterionMode::Require;
    std::uint32_t Evaluated = 0;
    std::uint32_t Rejected = 0;
    std::array<std::uint32_t, static_cast<std::size_t>(CandidateMeasureStatus::Count)> Statuses{};
    std::uint32_t ExactNavSearches = 0;
    std::uint32_t Raycasts = 0;
};

struct CandidateTraceValue
{
    bool Ran = false;
    float Value = 0.0f;
    CandidateMeasureStatus Status = CandidateMeasureStatus::NotApplicable;
    float CurveOutput = 0.0f;
    float Contribution = 0.0f;
    bool HasDetail = false;
    CandidateDetail Detail;
};

struct CandidateTraceRow
{
    Vec3d Position = Vec3d::Zero();
    EntityId Entity{};
    bool Projected = false;
    std::uint8_t Generator = 0;
    std::uint32_t Order = 0;
    CandidateTraceOutcome Outcome = CandidateTraceOutcome::Rejected;
    std::uint32_t Rank = 0;
    // Index into Criteria; meaningful when Outcome is Rejected.
    std::uint8_t RejectedBy = 0;
    CandidateMeasureStatus RejectStatus = CandidateMeasureStatus::Ok;
    float Score = 0.0f;
};

class CandidateTrace
{
public:
    CandidateTrace(CandidateTraceLevel level, std::uint32_t candidateCapacity, std::uint32_t criterionCapacity = 16);

    [[nodiscard]] CandidateTraceLevel Level() const { return Level_; }

    // Header.
    std::string Evaluation;
    EntityId Querier;
    ZoneId Zone;
    std::uint64_t Tick = 0;
    std::uint64_t Seed = 0;
    CandidateSelectionMode Selection = CandidateSelectionMode::Best;
    CandidateRunResult Result;
    std::uint32_t DroppedAtGeneration = 0;
    // Never compared: the one field that differs between identical runs.
    double ElapsedMicroseconds = 0.0;

    [[nodiscard]] std::span<const CandidateTraceCriterion> Criteria() const { return { Criteria_.data(), CriterionCount }; }

    // Full only, in generation order. Truncated when the run generated more
    // candidates than this trace holds.
    [[nodiscard]] std::span<const CandidateTraceRow> Rows() const { return { Rows_.data(), RowCount }; }
    [[nodiscard]] const CandidateTraceValue& Value(std::uint32_t row, std::uint32_t criterion) const
    {
        return Values_[static_cast<std::size_t>(criterion) * Rows_.size() + row];
    }
    [[nodiscard]] bool Truncated() const { return Truncated_; }

private:
    friend class CandidateEvaluator;
    friend class CandidateRun;

    void Reset();

    CandidateTraceLevel Level_;
    std::vector<CandidateTraceCriterion> Criteria_;
    std::uint32_t CriterionCount = 0;
    std::vector<CandidateTraceRow> Rows_;
    std::uint32_t RowCount = 0;
    std::vector<CandidateTraceValue> Values_;
    bool Truncated_ = false;
};
