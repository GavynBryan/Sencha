#include "../CandidateOperationSupport.h"

#include <cmath>

namespace candidate_operation
{
    namespace
    {
        struct GridState
        {
            std::uint8_t Center = 0;
            float Spacing = 1.0f;
            float Radius = 0.0f;
            Projection Projected;
        };

        CandidatePrepared PrepareGrid(const JsonValue& arguments, const CandidatePrepareContext& context,
                                      std::string_view subject, std::vector<std::string>& errors)
        {
            GridState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Spacing = FloatOr(arguments, "spacing", 1.0f);
            state.Radius = FloatOr(arguments, "radius", 0.0f);
            state.Projected = ReadProjection(arguments);
            if (!(state.Spacing > 0.0f))
                errors.push_back(std::string(subject) + ": spacing is positive");
            return Prepared(state);
        }

        // Row-major from -x -z, keeping the points within the radius.
        void GenerateGrid(CandidateRun& run, const void* state)
        {
            const auto& grid = *static_cast<const GridState*>(state);
            if (run.SlotSize(grid.Center) == 0)
                return;
            const Vec3d center = run.SlotPoint(grid.Center, 0).Position;
            const int steps = static_cast<int>(std::floor(grid.Radius / grid.Spacing));
            const float radiusSquared = grid.Radius * grid.Radius;
            for (int z = -steps; z <= steps; ++z)
            {
                for (int x = -steps; x <= steps; ++x)
                {
                    const float dx = static_cast<float>(x) * grid.Spacing;
                    const float dz = static_cast<float>(z) * grid.Spacing;
                    if (dx * dx + dz * dz > radiusSquared)
                        continue;
                    if (!AppendSample(run, center + Vec3d(dx, 0.0f, dz), grid.Projected))
                        return;
                }
            }
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
