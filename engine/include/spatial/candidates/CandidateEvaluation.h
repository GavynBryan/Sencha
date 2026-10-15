#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/BindingDependencyStamp.h>
#include <spatial/candidates/CandidateEvaluationDesc.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class DataAssetCache;

// A description bound into one World. Owns copies of everything it runs, so
// the description it came from may be replaced while this is held.
class CandidateEvaluation
{
public:
    struct BoundGenerator
    {
        CandidateGeneratorHandle Handle;
        CandidateGenerateFn Generate = nullptr;
        std::shared_ptr<const void> State;
    };

    struct BoundCriterion
    {
        CandidateCriterion Criterion;
        CandidateMeasureFn Measure = nullptr;
        std::shared_ptr<const void> State;
    };

    [[nodiscard]] std::string_view Name() const { return Name_; }

    // Index to bind in CandidateContext::Slots, or nullopt for an unknown name.
    [[nodiscard]] std::optional<std::uint8_t> FindSlot(std::string_view name) const;

    [[nodiscard]] std::span<const CandidateSlotDesc> Slots() const { return Slots_; }
    [[nodiscard]] std::span<const BoundGenerator> Generators() const { return Generators_; }
    [[nodiscard]] std::span<const BoundCriterion> Criteria() const { return Criteria_; }
    [[nodiscard]] const CandidateSelection& Selection() const { return Selection_; }
    [[nodiscard]] const CandidateBudgets& Budgets() const { return Budgets_; }

private:
    friend bool BindCandidateEvaluation(const CandidateEvaluationDesc&,
                                        const CandidateCatalogs&,
                                        const CandidateBindEnvironment&,
                                        CandidateEvaluation&,
                                        std::vector<std::string>&);

    std::string Name_;
    std::vector<CandidateSlotDesc> Slots_;
    std::vector<BoundGenerator> Generators_;
    std::vector<BoundCriterion> Criteria_;
    CandidateSelection Selection_;
    CandidateBudgets Budgets_;
};

// Owner thread. Collects every problem before returning false.
[[nodiscard]] bool BindCandidateEvaluation(const CandidateEvaluationDesc& desc,
                                           const CandidateCatalogs& catalogs,
                                           const CandidateBindEnvironment& environment,
                                           CandidateEvaluation& out,
                                           std::vector<std::string>& errors);

// A `candidates.evaluation` asset bound into one World, rebound when what it
// was bound against moves. Hold this across frames, never a cache pointer.
class CandidateEvaluationBinding
{
public:
    bool BindFrom(const DataAssetCache& cache,
                  DataAssetHandle asset,
                  const CandidateCatalogs& catalogs,
                  const CandidateBindEnvironment& environment,
                  std::vector<std::string>& errors);

    // True when it rebound. A few integer comparisons when nothing moved.
    bool Refresh(const DataAssetCache& cache,
                 const CandidateCatalogs& catalogs,
                 const CandidateBindEnvironment& environment,
                 std::vector<std::string>& errors);

    // Null when the last bind failed.
    [[nodiscard]] const CandidateEvaluation* Evaluation() const
    {
        return Bound.has_value() ? &*Bound : nullptr;
    }

private:
    DataAssetStamp Asset;
    CatalogStamp Measures;
    CatalogStamp Generators;
    CatalogStamp Queries;
    TagVocabularyStamp Tags;
    std::optional<CandidateEvaluation> Bound;
};
