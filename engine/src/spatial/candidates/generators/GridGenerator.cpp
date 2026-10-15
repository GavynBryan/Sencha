#include "../CandidateOperationSupport.h"

namespace candidate_operation
{
    namespace
    {
        struct GridState
        {
            std::uint8_t Center = 0;
            GridExtent Extent;
            Projection Projected;
        };

        CandidatePrepared PrepareGrid(const JsonValue& arguments, const CandidatePrepareContext& context,
                                      std::string_view subject, std::vector<std::string>& errors)
        {
            GridState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Extent = ReadGridExtent(arguments, subject, errors);
            state.Projected = ReadProjection(arguments);
            return Prepared(state);
        }

        // Row-major from -x -z, keeping the points within the radius.
        void GenerateGrid(CandidateRun& run, const void* state)
        {
            const auto& grid = *static_cast<const GridState*>(state);
            if (run.SlotSize(grid.Center) == 0)
                return;
            const Vec3d center = run.SlotPoint(grid.Center, 0).Position;
            ForEachGridOffset(run, grid.Extent, [&](const Vec3d& offset) {
                return AppendSample(run, center + offset, grid.Projected);
            });
        }
    }

    CandidateGeneratorDefinition GridGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.grid";
        definition.DisplayName = "Grid";
        definition.Description = "Points on a square grid within a radius of a slot, optionally projected onto "
                                 "the querier's navigation.";
        definition.Arguments = Record({ SlotField("center", "Center", "querier"),
                                        FloatField("spacing", "Spacing", std::nullopt, 0.0),
                                        FloatField("radius", "Radius", std::nullopt, 0.0) });
        for (DataFieldSchema& field : ProjectionFields())
            definition.Arguments.Children.push_back(std::move(field));
        definition.Prepare = PrepareGrid;
        definition.Generate = GenerateGrid;
        return definition;
    }
}
