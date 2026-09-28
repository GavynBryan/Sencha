#pragma once

#include <spatial/candidates/CandidateEvaluation.h>
#include <spatial/candidates/CandidateRun.h>
#include <spatial/candidates/CandidateScratch.h>
#include <spatial/candidates/CandidateTrace.h>
#include <spatial/candidates/CandidateTypes.h>

#include <optional>

class AuthoredQueryDispatcher;
class PhysicsWorld;
class RuntimeWorld;

// Owner-thread only; call from FixedLogic or PreSimulate, never during the
// physics step. Contract, freshness and zone rules: docs/spatial/candidates.md.
class CandidateEvaluator
{
public:
    CandidateEvaluator(const RuntimeWorld& world,
                       const CandidateCatalogs& catalogs,
                       const PhysicsWorld* physics = nullptr,
                       const AuthoredQueryDispatcher* queries = nullptr);

    // Non-const: the cached entity query refreshes its archetype list lazily.
    [[nodiscard]] CandidateRunResult Evaluate(const CandidateEvaluation& evaluation,
                                              const CandidateContext& context,
                                              CandidateScratch& scratch,
                                              CandidateResultBuffer& results,
                                              CandidateTrace* trace = nullptr);

private:
    friend class CandidateRun;

    [[nodiscard]] bool IsCurrent(const CandidateEvaluation& evaluation) const;
    [[nodiscard]] static bool Fits(const CandidateEvaluation& evaluation, const CandidateScratch& scratch);
    [[nodiscard]] static bool SlotsBound(const CandidateEvaluation& evaluation, const CandidateContext& context);

    void RunCriterion(CandidateRun& run, std::uint32_t criterion);
    void Score(CandidateRun& run);
    void Select(CandidateRun& run, CandidateResultBuffer& results, CandidateRunResult& result);
    void FillTrace(CandidateRun& run,
                   CandidateTrace& trace,
                   const CandidateRunResult& result,
                   const CandidateResultBuffer& results) const;

    const RuntimeWorld& Runtime;
    const CandidateCatalogs& Catalogs;
    const PhysicsWorld* Physics;
    const AuthoredQueryDispatcher* Queries;
    std::optional<CandidateTaggedEntityQuery> Tagged;
};
