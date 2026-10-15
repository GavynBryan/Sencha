#include "../CandidateOperationSupport.h"

#include <ecs/World.h>

namespace candidate_operation
{
    namespace
    {
        struct EntityTagsState
        {
            TagQueryNames Names;
            GameplayTagQuery Query;
        };

        CandidatePrepared PrepareEntityTags(const JsonValue& arguments, const CandidatePrepareContext&,
                                            std::string_view, std::vector<std::string>&)
        {
            return Prepared(EntityTagsState{ ReadTagQuery(arguments, "tags"), {} });
        }

        std::shared_ptr<const void> BindEntityTags(const void* prepared, const CandidateBindEnvironment& environment,
                                                   std::string_view subject, std::vector<std::string>& errors)
        {
            EntityTagsState bound = *static_cast<const EntityTagsState*>(prepared);
            if (!BindTagQuery(bound.Names, environment, subject, errors, bound.Query))
                return nullptr;
            return std::make_shared<const EntityTagsState>(std::move(bound));
        }

        void MeasureEntityTags(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& tags = *static_cast<const EntityTagsState*>(state);
            const GameplayTagRegistry* registry = run.Tags();
            static const GameplayTagContainer kNoTags{};
            for (const std::uint32_t row : rows)
            {
                const EntityId entity = run.Entity(row);
                if (registry == nullptr || !run.Entities().IsAlive(entity))
                {
                    statuses[row] = registry == nullptr ? CandidateMeasureStatus::NotApplicable
                                                        : CandidateMeasureStatus::Stale;
                    continue;
                }
                const GameplayTagContainer* held = run.Entities().TryGet<GameplayTagContainer>(entity);
                values[row] = tags.Query.Matches(held != nullptr ? *held : kNoTags, *registry) ? 1.0f : 0.0f;
                statuses[row] = CandidateMeasureStatus::Ok;
            }
        }
    }

    CandidateMeasureDefinition EntityTagsMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.entity_tags";
        definition.DisplayName = "Entity tags";
        definition.Description = "1 when the candidate entity's gameplay tags match a tag query, else 0.";
        definition.Arguments = Record({ TagQueryField("tags", "Tags") });
        definition.Cost = CandidateCostClass::Entity;
        definition.AppliesTo = CandidateAppliesTo::Entity;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.Prepare = PrepareEntityTags;
        definition.Bind = BindEntityTags;
        definition.Measure = MeasureEntityTags;
        return definition;
    }
}
