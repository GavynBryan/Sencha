#pragma once

#include <authored/AuthoredValue.h>
#include <ecs/EntityId.h>
#include <logic/selection/SelectionTable.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class AuthoredQueryDispatcher;
class World;

enum class SelectionStatus : std::uint8_t
{
    Success,
    NoneQualified,
    // Too few entities or inputs for the table, or a current option it does not have.
    ContextMissing,
};

enum class SelectionDecision : std::uint8_t
{
    // The best option, or the seeded pick from its band.
    Chosen,
    // The current option, still inside its commitment.
    Committed,
    // The current option, which no other beat by the switch margin.
    WithinMargin,
};

struct SelectionContext
{
    // In the table's entity order; index 0 is the querier.
    std::span<const EntityId> Entities;
    // In the table's input order.
    std::span<const float> Inputs;
    std::optional<std::uint32_t> Current;
    float CurrentSeconds = 0.0f;
    // PickFromBand only.
    std::uint64_t Seed = 0;
};

struct SelectionResult
{
    SelectionStatus Status = SelectionStatus::ContextMissing;
    std::uint32_t Option = 0;
    float Score = 0.0f;
    SelectionDecision Decision = SelectionDecision::Chosen;
    bool Interrupted = false;
};

enum class SelectionValueStatus : std::uint8_t
{
    Measured,
    Unmeasured,
    // Skipped once an earlier requirement disqualified the option.
    NotEvaluated,
};

struct SelectionTraceValue
{
    float Value = 0.0f;
    float Output = 0.0f;
    SelectionValueStatus Status = SelectionValueStatus::NotEvaluated;
};

struct SelectionTraceOption
{
    bool Qualified = false;
    // The consideration that disqualified it, when one did.
    std::optional<std::uint32_t> RejectedBy;
    float Score = 0.0f;
    // Into SelectionTrace::Values, one per consideration in authored order.
    std::uint32_t FirstValue = 0;
};

// One run, as plain owned data. Filling it never changes the result.
struct SelectionTrace
{
    std::vector<SelectionTraceOption> Options;
    std::vector<SelectionTraceValue> Values;
    SelectionResult Result;
};

// Caller-owned and reused, so a steady state allocates nothing.
struct SelectionScratch
{
    AuthoredArguments Arguments;
    AuthoredValue Answer;
    std::vector<float> Scores;
    std::vector<std::uint8_t> Qualified;
};

// Owner thread; a pure read of the World and the queries it asks.
class SelectionEvaluator
{
public:
    SelectionEvaluator(const World& world, const AuthoredQueryDispatcher* queries);

    [[nodiscard]] SelectionResult Evaluate(const SelectionTable& table, const SelectionContext& context,
                                           SelectionScratch& scratch, SelectionTrace* trace = nullptr) const;

private:
    [[nodiscard]] std::optional<float> Measure(const SelectionConsideration& consideration,
                                               const SelectionContext& context, SelectionScratch& scratch) const;
    [[nodiscard]] bool Interrupted(const SelectionTable& table, EntityId querier) const;

    const World& Entities;
    const AuthoredQueryDispatcher* Queries;
};
