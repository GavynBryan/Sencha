#include "../CandidateOperationSupport.h"

#include <navigation/NavigationQuery.h>

#include <cmath>

namespace candidate_operation
{
    namespace
    {
        enum class Sampling : std::uint8_t
        {
            Grid,
            PerRegion,
        };

        struct ReachableState
        {
            std::uint8_t Center = 0;
            float Radius = 0.0f;
            float MaxCost = 0.0f;
            Sampling Mode = Sampling::Grid;
            float Spacing = 1.0f;
            float ProjectHeight = 2.0f;
        };

        CandidatePrepared PrepareReachable(const JsonValue& arguments, const CandidatePrepareContext& context,
                                           std::string_view subject, std::vector<std::string>& errors)
        {
            ReachableState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Radius = FloatOr(arguments, "radius", 0.0f);
            state.MaxCost = FloatOr(arguments, "max_cost", 0.0f);
            state.Mode = StringOr(arguments, "mode", "grid") == "per_region" ? Sampling::PerRegion : Sampling::Grid;
            state.Spacing = FloatOr(arguments, "spacing", 1.0f);
            state.ProjectHeight = FloatOr(arguments, "project_height", 2.0f);
            if (!(state.Spacing > 0.0f))
                errors.push_back(std::string(subject) + ": spacing is positive");
            return Prepared(state);
        }

        void SampleGrid(CandidateRun& run, const ReachableState& reachable, const Vec3d& center)
        {
            const ZoneNavigation& navigation = *run.Navigation();
            const Vec3d extents(0.5f, reachable.ProjectHeight, 0.5f);
            const int steps = static_cast<int>(std::floor(reachable.Radius / reachable.Spacing));
            const float radiusSquared = reachable.Radius * reachable.Radius;
            for (int z = -steps; z <= steps; ++z)
            {
                for (int x = -steps; x <= steps; ++x)
                {
                    const float dx = static_cast<float>(x) * reachable.Spacing;
                    const float dz = static_cast<float>(z) * reachable.Spacing;
                    if (dx * dx + dz * dz > radiusSquared)
                        continue;
                    const NavProjectResult projected = NavProjectPoint(
                        navigation, run.NavContext(), run.NavRequest(), center + Vec3d(dx, 0.0f, dz), extents);
                    if (projected.Status != NavStatus::Success || run.FindReachable(projected.Location.Ref) == nullptr)
                    {
                        run.CountDroppedAtGeneration();
                        continue;
                    }
                    if (!run.Append(projected.Location.Position, {}, projected.Location))
                        return;
                }
            }
        }

        // One point per region, cheapest region first: the region's closest point to the center.
        void SamplePerRegion(CandidateRun& run, const ReachableState& reachable, const Vec3d& center,
                             std::span<const NavReachableRegion> regions)
        {
            const ZoneNavigation& navigation = *run.Navigation();
            for (const NavReachableRegion& region : regions)
            {
                const NavProjectResult closest = NavClosestPointInRegion(navigation, run.NavContext(), region.Region, center);
                if (closest.Status != NavStatus::Success
                    || (closest.Location.Position - center).Magnitude() > reachable.Radius)
                    continue;
                if (!run.Append(closest.Location.Position, {}, closest.Location))
                    return;
            }
        }

        void GenerateReachable(CandidateRun& run, const void* state)
        {
            const auto& reachable = *static_cast<const ReachableState*>(state);
            if (run.Navigation() == nullptr || run.SlotSize(reachable.Center) == 0)
                return;
            const Vec3d center = run.SlotPoint(reachable.Center, 0).Position;
            // Navigation filters regions by distance from the search's start;
            // widen so every region within the radius of the center survives.
            const float searchRadius = (center - run.QuerierPosition()).Magnitude() + reachable.Radius;
            const auto regions = run.ReachableRegions(searchRadius, reachable.MaxCost);
            if (!regions)
                return;
            if (reachable.Mode == Sampling::Grid)
                SampleGrid(run, reachable, center);
            else
                SamplePerRegion(run, reachable, center, *regions);
        }
    }

    CandidateGeneratorDefinition ReachableGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.reachable";
        definition.DisplayName = "Reachable points";
        definition.Description = "Points on the querier's navigation it can reach within a cost budget, "
                                 "sampled on a grid or one per region, within a radius of a slot.";
        definition.Arguments = Record({ SlotField("center", "Center", "querier"),
                                        FloatField("radius", "Radius", std::nullopt, 0.0),
                                        FloatField("max_cost", "Maximum cost", std::nullopt, 0.0),
                                        EnumField("mode", "Sampling", { "grid", "per_region" }, "grid"),
                                        FloatField("spacing", "Grid spacing", 1.0, 0.0),
                                        FloatField("project_height", "Projection height", 2.0, 0.0) });
        definition.Prepare = PrepareReachable;
        definition.Generate = GenerateReachable;
        return definition;
    }
}
