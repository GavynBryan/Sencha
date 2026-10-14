#include <spatial/candidates/CandidateEvaluator.h>

#include <core/hash/ContentHash.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>

namespace
{
    constexpr std::uint8_t kAlive = 0xFF;

    [[nodiscard]] bool Applies(CandidateAppliesTo appliesTo, EntityId entity)
    {
        switch (appliesTo)
        {
        case CandidateAppliesTo::Any: return true;
        case CandidateAppliesTo::Location: return !entity.IsValid();
        case CandidateAppliesTo::Entity: return entity.IsValid();
        }
        return false;
    }

    // Independent of evaluation order and thread count: a pure function of
    // the caller's seed and the candidate's generation order.
    [[nodiscard]] std::uint64_t BandKey(std::uint64_t seed, std::uint32_t order)
    {
        const std::array<std::byte, sizeof(order)> bytes = std::bit_cast<std::array<std::byte, sizeof(order)>>(order);
        return HashBytes64(bytes, seed);
    }
}

CandidateEvaluator::CandidateEvaluator(const RuntimeWorld& world,
                                       const CandidateCatalogs& catalogs,
                                       const PhysicsWorld* physics,
                                       const AuthoredQueryDispatcher* queries)
    : Runtime(world)
    , Catalogs(catalogs)
    , Physics(physics)
    , Queries(queries)
{
}

bool CandidateEvaluator::IsCurrent(const CandidateEvaluation& evaluation) const
{
    for (const CandidateEvaluation::BoundGenerator& generator : evaluation.Generators())
    {
        if (!Catalogs.Generators().IsCurrent(generator.Handle))
            return false;
    }
    for (const CandidateEvaluation::BoundCriterion& criterion : evaluation.Criteria())
    {
        if (!Catalogs.Measures().IsCurrent(criterion.Criterion.Handle))
            return false;
    }
    return true;
}

bool CandidateEvaluator::Fits(const CandidateEvaluation& evaluation, const CandidateScratch& scratch)
{
    const CandidateLimits& limits = scratch.Limits();
    const CandidateBudgets& budgets = evaluation.Budgets();
    return budgets.Candidates <= limits.MaxCandidates && evaluation.Criteria().size() <= limits.MaxCriteria
        && budgets.ReachableRegions <= limits.MaxReachableRegions && evaluation.Generators().size() <= kAlive;
}

bool CandidateEvaluator::SlotsBound(const CandidateEvaluation& evaluation, const CandidateContext& context)
{
    const std::span<const CandidateSlotDesc> slots = evaluation.Slots();
    for (std::size_t index = 1; index < slots.size(); ++index)
    {
        if (!slots[index].Required)
            continue;
        const bool bound = std::ranges::any_of(context.Slots, [index](const CandidateSlotBinding& binding) {
            return binding.Slot == index && !binding.Points.empty();
        });
        if (!bound)
            return false;
    }
    return true;
}

CandidateRunResult CandidateEvaluator::Evaluate(const CandidateEvaluation& evaluation,
                                                const CandidateContext& context,
                                                CandidateScratch& scratch,
                                                CandidateResultBuffer& results,
                                                CandidateTrace* trace)
{
    const auto started = std::chrono::steady_clock::now();
    results.Count = 0;
    if (trace != nullptr)
        trace->Reset();

    CandidateRunResult result;
    const auto refuse = [&](CandidateRunStatus status) {
        result.Status = status;
        if (trace != nullptr)
            trace->Result = result;
        return result;
    };
    if (!IsCurrent(evaluation))
        return refuse(CandidateRunStatus::DefinitionStale);
    if (!Fits(evaluation, scratch) || (trace != nullptr && trace->Criteria_.size() < evaluation.Criteria().size()))
        return refuse(CandidateRunStatus::ScratchTooSmall);
    if (!SlotsBound(evaluation, context))
        return refuse(CandidateRunStatus::ContextMissing);
    const ZoneId zone = context.Navigation.Zone;
    if (zone.IsValid() && !Runtime.IsZoneResident(zone))
        return refuse(CandidateRunStatus::QuerierZoneUnavailable);

    CandidateRun run(*this, evaluation, context, scratch, trace);
    run.Flags.NavigationUnavailable = run.ZoneNav == nullptr;

    const std::span<const CandidateEvaluation::BoundGenerator> generators = evaluation.Generators();
    for (std::size_t index = 0; index < generators.size() && !run.Flags.GenerationTruncated; ++index)
    {
        run.CurrentGenerator = static_cast<std::uint8_t>(index);
        generators[index].Generate(run, generators[index].State.get());
    }
    result.Generated = scratch.Count;

    scratch.Ranked.clear();
    scratch.Live.clear();
    for (std::uint32_t row = 0; row < scratch.Count; ++row)
        scratch.Live.push_back(row);
    for (std::uint32_t criterion = 0; criterion < evaluation.Criteria().size() && !scratch.Live.empty(); ++criterion)
    {
        RunCriterion(run, criterion);
        if (run.DefinitionStale)
            break;
    }
    result.Qualified = static_cast<std::uint32_t>(scratch.Live.size());

    if (run.DefinitionStale)
        result.Status = CandidateRunStatus::DefinitionStale;
    else if (result.Generated == 0)
        result.Status = CandidateRunStatus::NoCandidates;
    else if (result.Qualified == 0)
        result.Status = CandidateRunStatus::NoneQualified;
    else
    {
        Score(run);
        Select(run, results, result);
        result.Status = CandidateRunStatus::Success;
    }
    result.Flags = run.Flags;

    if (trace != nullptr)
    {
        FillTrace(run, *trace, result, results);
        trace->ElapsedMicroseconds =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    }
    return result;
}

void CandidateEvaluator::RunCriterion(CandidateRun& run, std::uint32_t index)
{
    CandidateScratch& scratch = run.Scratch;
    const CandidateEvaluation::BoundCriterion& bound = run.Evaluation.Criteria()[index];
    const CandidateCriterion& criterion = bound.Criterion;
    const std::size_t capacity = scratch.Limits().MaxCandidates;
    const std::span<float> values(scratch.Values.data() + index * capacity, capacity);
    const std::span<CandidateMeasureStatus> statuses(scratch.Statuses.data() + index * capacity, capacity);
    const std::span<float> curves(scratch.CurveOutputs.data() + index * capacity, capacity);
    run.CurrentCriterion = index;

    scratch.Applicable.clear();
    for (const std::uint32_t row : scratch.Live)
    {
        values[row] = 0.0f;
        curves[row] = 0.0f;
        if (Applies(criterion.AppliesTo, scratch.Entities[row]))
            scratch.Applicable.push_back(row);
        else
            statuses[row] = CandidateMeasureStatus::NotApplicable;
    }
    if (!scratch.Applicable.empty())
        bound.Measure(run, bound.State.get(), scratch.Applicable, values, statuses);

    CandidateTraceCriterion* traced =
        run.Trace != nullptr ? &run.Trace->Criteria_[index] : nullptr;
    if (traced != nullptr)
        traced->Evaluated = static_cast<std::uint32_t>(scratch.Live.size());

    std::size_t kept = 0;
    for (const std::uint32_t row : scratch.Live)
    {
        const CandidateMeasureStatus status = statuses[row];
        if (traced != nullptr)
            ++traced->Statuses[static_cast<std::size_t>(status)];
        if (status == CandidateMeasureStatus::Estimated)
            run.MarkEstimated();

        bool keep = true;
        if (!IsMeasured(status))
        {
            keep = criterion.UnmeasuredScore.has_value();
            curves[row] = criterion.UnmeasuredScore.value_or(0.0f);
        }
        else if (criterion.Mode == CandidateCriterionMode::Require)
            keep = values[row] >= criterion.Min && values[row] <= criterion.Max;
        else
            curves[row] = criterion.Curve ? EvaluateResponseCurve(*criterion.Curve, values[row])
                                          : std::clamp(values[row], 0.0f, 1.0f);

        if (keep)
            scratch.Live[kept++] = row;
        else
        {
            scratch.RejectedBy[row] = static_cast<std::uint8_t>(index);
            scratch.RejectStatus[row] = status;
        }
    }
    if (traced != nullptr)
        traced->Rejected = traced->Evaluated - static_cast<std::uint32_t>(kept);
    scratch.Live.resize(kept);
}

void CandidateEvaluator::Score(CandidateRun& run)
{
    CandidateScratch& scratch = run.Scratch;
    const std::span<const CandidateEvaluation::BoundCriterion> criteria = run.Evaluation.Criteria();
    const std::size_t capacity = scratch.Limits().MaxCandidates;

    // Summed in authored order, so identical inputs give bit-identical scores.
    std::array<std::uint32_t, 256> scored{};
    std::size_t scoredCount = 0;
    float weightSum = 0.0f;
    for (std::uint32_t index = 0; index < criteria.size(); ++index)
    {
        if (criteria[index].Criterion.Mode != CandidateCriterionMode::Score)
            continue;
        scored[scoredCount++] = index;
        weightSum += criteria[index].Criterion.Weight;
    }
    std::sort(scored.begin(), scored.begin() + scoredCount, [&](std::uint32_t a, std::uint32_t b) {
        return criteria[a].Criterion.AuthoredIndex < criteria[b].Criterion.AuthoredIndex;
    });

    for (const std::uint32_t row : scratch.Live)
    {
        if (scoredCount == 0)
        {
            scratch.Scores[row] = 1.0f;
            continue;
        }
        float sum = 0.0f;
        for (std::size_t i = 0; i < scoredCount; ++i)
        {
            const std::uint32_t index = scored[i];
            sum += criteria[index].Criterion.Weight * scratch.CurveOutputs[index * capacity + row];
        }
        scratch.Scores[row] = sum / weightSum;
    }
}

void CandidateEvaluator::Select(CandidateRun& run, CandidateResultBuffer& results, CandidateRunResult& result)
{
    CandidateScratch& scratch = run.Scratch;
    scratch.Ranked.assign(scratch.Live.begin(), scratch.Live.end());
    // Exact comparison, then generation order: a strict total order, so the
    // ranking is a pure function of the inputs.
    std::sort(scratch.Ranked.begin(), scratch.Ranked.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (scratch.Scores[a] != scratch.Scores[b])
            return scratch.Scores[a] > scratch.Scores[b];
        return a < b;
    });

    const CandidateSelection& selection = run.Evaluation.Selection();
    const auto emit = [&](std::uint32_t rank) {
        const std::uint32_t row = scratch.Ranked[rank];
        results.Entries_[results.Count++] = CandidateResultEntry{ scratch.Positions[row], scratch.Entities[row],
                                                                  scratch.Navs[row], scratch.Scores[row],
                                                                  row, rank };
    };

    if (selection.Mode == CandidateSelectionMode::PickFromBand)
    {
        const float floor = scratch.Scores[scratch.Ranked.front()] - selection.BandWidth;
        std::uint32_t pick = 0;
        std::uint64_t pickKey = BandKey(run.Context.Seed, scratch.Ranked.front());
        for (std::uint32_t rank = 1; rank < scratch.Ranked.size() && scratch.Scores[scratch.Ranked[rank]] >= floor;
             ++rank)
        {
            const std::uint64_t key = BandKey(run.Context.Seed, scratch.Ranked[rank]);
            if (key < pickKey)
            {
                pick = rank;
                pickKey = key;
            }
        }
        if (results.Capacity() > 0)
            emit(pick);
        else
            run.Flags.OutputTruncated = true;
    }
    else
    {
        const std::size_t wanted = selection.Mode == CandidateSelectionMode::AllQualified
            ? scratch.Ranked.size()
            : std::min<std::size_t>(selection.Count, scratch.Ranked.size());
        const std::size_t returned = std::min<std::size_t>(wanted, results.Capacity());
        run.Flags.OutputTruncated = returned < wanted;
        for (std::uint32_t rank = 0; rank < returned; ++rank)
            emit(rank);
    }
    result.Returned = results.Count;
}

void CandidateEvaluator::FillTrace(CandidateRun& run,
                                   CandidateTrace& trace,
                                   const CandidateRunResult& result,
                                   const CandidateResultBuffer& results) const
{
    const CandidateScratch& scratch = run.Scratch;
    const CandidateContext& context = run.Context;
    const std::span<const CandidateEvaluation::BoundCriterion> criteria = run.Evaluation.Criteria();

    trace.Evaluation.assign(run.Evaluation.Name());
    trace.Querier = context.Querier;
    trace.Zone = context.Navigation.Zone;
    trace.Tick = context.Tick;
    trace.Seed = context.Seed;
    trace.Selection = run.Evaluation.Selection().Mode;
    trace.Result = result;
    trace.DroppedAtGeneration = run.DroppedAtGeneration;
    trace.CriterionCount = static_cast<std::uint32_t>(criteria.size());
    for (std::size_t index = 0; index < criteria.size(); ++index)
    {
        trace.Criteria_[index].Measure.assign(criteria[index].Criterion.Operation);
        trace.Criteria_[index].Mode = criteria[index].Criterion.Mode;
    }
    if (trace.Level() != CandidateTraceLevel::Full)
        return;

    const std::uint32_t capacity = static_cast<std::uint32_t>(trace.Rows_.size());
    trace.RowCount = std::min(scratch.Count, capacity);
    trace.Truncated_ = scratch.Count > capacity;
    const std::size_t columns = scratch.Limits().MaxCandidates;

    float weightSum = 0.0f;
    for (const CandidateEvaluation::BoundCriterion& criterion : criteria)
    {
        if (criterion.Criterion.Mode == CandidateCriterionMode::Score)
            weightSum += criterion.Criterion.Weight;
    }

    for (std::uint32_t row = 0; row < trace.RowCount; ++row)
    {
        CandidateTraceRow& traced = trace.Rows_[row];
        traced = CandidateTraceRow{};
        traced.Position = scratch.Positions[row];
        traced.Entity = scratch.Entities[row];
        traced.Projected = scratch.Navs[row].IsValid();
        traced.Generator = scratch.Generators[row];
        traced.Order = row;
        traced.RejectedBy = scratch.RejectedBy[row];
        traced.RejectStatus = scratch.RejectStatus[row];
        const bool survived = scratch.RejectedBy[row] == kAlive;
        traced.Outcome = survived ? CandidateTraceOutcome::RankedNotReturned : CandidateTraceOutcome::Rejected;
        traced.Score = survived ? scratch.Scores[row] : 0.0f;

        for (std::uint32_t index = 0; index < criteria.size(); ++index)
        {
            CandidateTraceValue& value = trace.Values_[static_cast<std::size_t>(index) * capacity + row];
            value.Ran = survived || index <= scratch.RejectedBy[row];
            if (!value.Ran)
                continue;
            const std::size_t cell = index * columns + row;
            value.Value = scratch.Values[cell];
            value.Status = scratch.Statuses[cell];
            value.CurveOutput = scratch.CurveOutputs[cell];
            const CandidateCriterion& criterion = criteria[index].Criterion;
            if (survived && criterion.Mode == CandidateCriterionMode::Score && weightSum > 0.0f)
                value.Contribution = criterion.Weight * value.CurveOutput / weightSum;
        }
    }
    for (std::uint32_t rank = 0; rank < scratch.Ranked.size() && result.Qualified > 0; ++rank)
    {
        if (scratch.Ranked[rank] < trace.RowCount)
            trace.Rows_[scratch.Ranked[rank]].Rank = rank;
    }
    for (const CandidateResultEntry& entry : results.Entries())
    {
        if (entry.Order < trace.RowCount)
            trace.Rows_[entry.Order].Outcome = CandidateTraceOutcome::Selected;
    }
}
