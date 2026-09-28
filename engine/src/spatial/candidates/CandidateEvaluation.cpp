#include <spatial/candidates/CandidateEvaluation.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredQueryRegistry.h>
#include <spatial/candidates/CandidateEvaluationData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>

namespace
{
    // A bind function returning null with no new error keeps the prepared state.
    template<typename Definition>
    std::shared_ptr<const void> BindState(const Definition& definition,
                                          const std::shared_ptr<const void>& prepared,
                                          const CandidateBindEnvironment& environment,
                                          std::string_view subject,
                                          std::vector<std::string>& errors,
                                          bool& ok)
    {
        if (definition.Bind == nullptr)
            return prepared;
        const std::size_t before = errors.size();
        std::shared_ptr<const void> bound = definition.Bind(prepared.get(), environment, subject, errors);
        if (errors.size() != before)
        {
            ok = false;
            return nullptr;
        }
        return bound != nullptr ? bound : prepared;
    }
}

std::optional<std::uint8_t> CandidateEvaluation::FindSlot(std::string_view name) const
{
    for (std::size_t index = 0; index < Slots_.size(); ++index)
    {
        if (Slots_[index].Name == name)
            return static_cast<std::uint8_t>(index);
    }
    return std::nullopt;
}

bool BindCandidateEvaluation(const CandidateEvaluationDesc& desc,
                             const CandidateCatalogs& catalogs,
                             const CandidateBindEnvironment& environment,
                             CandidateEvaluation& out,
                             std::vector<std::string>& errors)
{
    CandidateEvaluation bound;
    bound.Name_ = desc.Name;
    bound.Slots_ = desc.Slots;
    bound.Selection_ = desc.Selection;
    bound.Budgets_ = desc.Budgets;

    bool ok = true;
    for (std::size_t index = 0; index < desc.Generators.size(); ++index)
    {
        const CandidateGeneratorStep& step = desc.Generators[index];
        const std::string subject = std::format("generator {} '{}'", index, step.Operation);
        if (!catalogs.Generators().IsCurrent(step.Handle))
        {
            errors.push_back(std::format("{}: the generator changed since this evaluation compiled", subject));
            ok = false;
            continue;
        }
        const CandidateGeneratorDefinition& definition = *catalogs.Generators().Get(step.Handle.Slot);
        std::shared_ptr<const void> state = BindState(definition, step.Prepared, environment, subject, errors, ok);
        bound.Generators_.push_back({ step.Handle, definition.Generate, std::move(state) });
    }

    for (const CandidateCriterion& criterion : desc.Criteria)
    {
        const std::string subject = std::format("criterion {} '{}'", criterion.AuthoredIndex, criterion.Operation);
        if (!catalogs.Measures().IsCurrent(criterion.Handle))
        {
            errors.push_back(std::format("{}: the measure changed since this evaluation compiled", subject));
            ok = false;
            continue;
        }
        const CandidateMeasureDefinition& definition = *catalogs.Measures().Get(criterion.Handle.Slot);
        std::shared_ptr<const void> state = BindState(definition, criterion.Prepared, environment, subject, errors, ok);
        bound.Criteria_.push_back({ criterion, definition.Measure, std::move(state) });
    }

    if (!ok)
        return false;
    out = std::move(bound);
    return true;
}

bool CandidateEvaluationBinding::BindFrom(const DataAssetCache& cache,
                                          DataAssetHandle asset,
                                          const CandidateCatalogs& catalogs,
                                          const CandidateBindEnvironment& environment,
                                          std::vector<std::string>& errors)
{
    Asset = DataAssetStamp::Of(cache, asset);
    Measures = CatalogStamp::Of(catalogs.Measures());
    Generators = CatalogStamp::Of(catalogs.Generators());
    Queries = environment.Queries != nullptr ? CatalogStamp::Of(*environment.Queries) : CatalogStamp{};
    Tags = environment.Tags != nullptr ? TagVocabularyStamp::Of(*environment.Tags) : TagVocabularyStamp{};
    Bound.reset();

    const auto* desc = cache.TryGet<CandidateEvaluationDesc>(asset, kCandidateEvaluationTypeName);
    if (desc == nullptr)
    {
        errors.push_back(std::format("'{}' is not a resident candidate evaluation", cache.GetName(asset)));
        return false;
    }
    CandidateEvaluation evaluation;
    if (!BindCandidateEvaluation(*desc, catalogs, environment, evaluation, errors))
        return false;
    Bound = std::move(evaluation);
    return true;
}

bool CandidateEvaluationBinding::Refresh(const DataAssetCache& cache,
                                         const CandidateCatalogs& catalogs,
                                         const CandidateBindEnvironment& environment,
                                         std::vector<std::string>& errors)
{
    bool changed = !Asset.Matches(cache) || !Measures.Matches(catalogs.Measures())
        || !Generators.Matches(catalogs.Generators());
    if (environment.Queries != nullptr)
        changed = changed || !Queries.Matches(*environment.Queries);
    if (environment.Tags != nullptr)
        changed = changed || !Tags.Matches(*environment.Tags);
    if (!changed)
        return false;
    (void)BindFrom(cache, Asset.Asset, catalogs, environment, errors);
    return true;
}
