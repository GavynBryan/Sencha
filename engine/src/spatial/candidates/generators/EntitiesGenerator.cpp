#include "../CandidateOperationSupport.h"

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <limits>

namespace candidate_operation
{
    namespace
    {
        struct EntitiesState
        {
            std::uint8_t Center = 0;
            float Radius = std::numeric_limits<float>::infinity();
            TagQueryNames Names;
            GameplayTagQuery Tags;
            bool IncludeQuerier = false;
            bool AllResident = false;
        };

        [[nodiscard]] bool Before(EntityId left, EntityId right)
        {
            return left.Index != right.Index ? left.Index < right.Index : left.Generation < right.Generation;
        }

        CandidatePrepared PrepareEntities(const JsonValue& arguments, const CandidatePrepareContext& context,
                                          std::string_view subject, std::vector<std::string>& errors)
        {
            EntitiesState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Radius = FloatOr(arguments, "radius", std::numeric_limits<float>::infinity());
            state.Names = ReadTagQuery(arguments, "tags");
            state.IncludeQuerier = BoolOr(arguments, "include_querier", false);
            state.AllResident = BoolOr(arguments, "all_resident", false);
            return Prepared(std::move(state));
        }

        std::shared_ptr<const void> BindEntities(const void* prepared, const CandidateBindEnvironment& environment,
                                                 std::string_view subject, std::vector<std::string>& errors)
        {
            EntitiesState bound = *static_cast<const EntitiesState*>(prepared);
            if (!BindTagQuery(bound.Names, environment, subject, errors, bound.Tags))
                return nullptr;
            return std::make_shared<const EntitiesState>(std::move(bound));
        }

        // Keeps the lowest ids in a bounded max-heap, so which entities survive
        // truncation depends on identity, never on chunk layout.
        void GenerateEntities(CandidateRun& run, const void* state)
        {
            const auto& entities = *static_cast<const EntitiesState*>(state);
            if (run.SlotSize(entities.Center) == 0)
                return;
            const Vec3d center = run.SlotPoint(entities.Center, 0).Position;
            const float radiusSquared = entities.Radius * entities.Radius;
            const GameplayTagRegistry* registry = run.Tags();
            const bool filterTags = !entities.Names.Empty() && registry != nullptr;
            const std::uint32_t room = run.Room();

            std::vector<EntityId>& selected = run.EntitySelection();
            selected.clear();
            bool overflowed = false;
            const auto consider = [&](EntityId entity) {
                if (selected.size() < room)
                {
                    selected.push_back(entity);
                    std::ranges::push_heap(selected, Before);
                    return;
                }
                overflowed = true;
                if (room == 0 || !Before(entity, selected.front()))
                    return;
                std::ranges::pop_heap(selected, Before);
                selected.back() = entity;
                std::ranges::push_heap(selected, Before);
            };
            const auto visit = [&](auto& view) {
                const auto transforms = view.template Read<WorldTransform>();
                const auto tags = view.template Read<GameplayTagContainer>();
                for (std::uint32_t row = 0; row < view.Count(); ++row)
                {
                    const EntityId entity = view.Entity(row);
                    if (!entities.IncludeQuerier && entity == run.Querier())
                        continue;
                    if ((transforms[row].Value.Position - center).SqrMagnitude() > radiusSquared)
                        continue;
                    if (filterTags && !entities.Tags.Matches(tags[row], *registry))
                        continue;
                    consider(entity);
                }
            };
            if (entities.AllResident)
                run.TaggedEntities().ForEachChunk(visit);
            else
                run.TaggedEntities().ForEachChunkIn(run.Partitions(), visit);

            std::ranges::sort_heap(selected, Before);
            const World& world = run.Entities();
            for (const EntityId entity : selected)
                (void)run.Append(world.TryGet<WorldTransform>(entity)->Value.Position, entity);
            if (overflowed)
                run.MarkGenerationTruncated();
        }
    }

    CandidateGeneratorDefinition EntitiesGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.entities";
        definition.DisplayName = "Tagged entities";
        definition.Description = "Entities with a transform and gameplay tags matching a tag query, within a "
                                 "radius of a slot, in ascending entity order.";
        definition.Arguments = Record({ SlotField("center", "Center", "querier"),
                                        FloatField("radius", "Radius", std::nullopt, 0.0),
                                        TagQueryField("tags", "Tags"),
                                        BoolField("include_querier", "Include the querier", false),
                                        BoolField("all_resident", "Every resident zone", false) });
        definition.Arguments.Children[1].Required = false;
        definition.Prepare = PrepareEntities;
        definition.Bind = BindEntities;
        definition.Generate = GenerateEntities;
        return definition;
    }
}
