#include "../CandidateOperationSupport.h"

#include <math/Angle.h>
#include <physics/LevelGeometry.h>

#include <algorithm>
#include <cmath>

namespace candidate_operation
{
    namespace
    {
        struct SurfaceState
        {
            std::uint8_t Center = 0;
            float Reach = 18.0f;
            int Rays = 24;
            int Rows = 3;
            float LowestPitch = 0.0f;
            float HighestPitch = 45.0f;
            float Eye = 1.3f;
            float Standoff = 0.55f;
            float MostSlope = 0.3f;
            float Drop = 12.0f;
        };

        CandidatePrepared PrepareSurface(const JsonValue& arguments, const CandidatePrepareContext& context,
                                         std::string_view subject, std::vector<std::string>& errors)
        {
            SurfaceState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Reach = FloatOr(arguments, "reach", state.Reach);
            state.Rays = static_cast<int>(FloatOr(arguments, "rays", 24.0f));
            state.Rows = static_cast<int>(FloatOr(arguments, "rows", 3.0f));
            state.LowestPitch = FloatOr(arguments, "lowest_pitch", state.LowestPitch);
            state.HighestPitch = FloatOr(arguments, "highest_pitch", state.HighestPitch);
            state.Eye = FloatOr(arguments, "eye", state.Eye);
            state.Standoff = FloatOr(arguments, "standoff", state.Standoff);
            state.MostSlope = FloatOr(arguments, "most_slope", state.MostSlope);
            state.Drop = FloatOr(arguments, "drop", state.Drop);
            if (!(state.Reach > 0.0f) || state.Rays <= 0 || state.Rows <= 0 || !(state.Drop > 0.0f))
                errors.push_back(std::string(subject) + ": reach, rays, rows and drop are positive");
            return Prepared(state);
        }

        // Rays out from the center, kept where they meet a wall of the level with
        // navigation below it to come down to. The ray proves a straight line
        // reaches the wall, which is why nothing here asks navigation how to get there.
        void GenerateSurface(CandidateRun& run, const void* state)
        {
            const auto& surface = *static_cast<const SurfaceState*>(state);
            const PhysicsQueries* physics = run.Physics();
            if (physics == nullptr || run.SlotSize(surface.Center) == 0)
                return;
            const std::array<EntityId, 1> ignored{ run.Querier() };
            const PhysicsQueryFilter filter{ .IncludeTriggers = false, .IgnoreEntities = ignored, .IncludeCharacters = false };
            const Vec3d origin = run.SlotPoint(surface.Center, 0).Position + Vec3d::Up() * surface.Eye;
            for (int row = 0; row < surface.Rows; ++row)
            {
                const float share = surface.Rows > 1 ? static_cast<float>(row) / static_cast<float>(surface.Rows - 1) : 0.0f;
                const float pitch = ToRadians(surface.LowestPitch + (surface.HighestPitch - surface.LowestPitch) * share);
                for (int ray = 0; ray < surface.Rays; ++ray)
                {
                    if (!run.ChargeGenerationAttempt() || !run.ChargeRaycast())
                        return;
                    const float yaw = ToRadians(360.0f * static_cast<float>(ray) / static_cast<float>(surface.Rays));
                    const Vec3d direction(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
                    const RaycastHit hit = physics->Raycast(origin, direction, surface.Reach, filter);
                    if (!hit.Hit || !IsLevelGeometry(run.Entities(), hit.Entity) || std::abs(hit.Normal.Y) > surface.MostSlope)
                    {
                        run.CountDroppedAtGeneration();
                        continue;
                    }
                    const Vec3d position = hit.Point + hit.Normal * surface.Standoff;
                    const Vec3d clear = hit.Point + hit.Normal * (surface.Standoff + 0.6f);
                    if (!run.ChargeRaycast())
                        return;
                    const RaycastHit ground = physics->Raycast(clear, -Vec3d::Up(), surface.Drop, filter);
                    const std::optional<NavLocation> landing =
                        ground.Hit ? run.ProjectNav(ground.Point) : std::nullopt;
                    if (!landing)
                    {
                        run.CountDroppedAtGeneration();
                        continue;
                    }
                    if (!run.Append(position, {}, *landing))
                        return;
                }
            }
        }
    }

    CandidateGeneratorDefinition SurfaceGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.surface";
        definition.DisplayName = "Wall surface";
        definition.Description = "Rays fanned out from a slot, kept where they meet a wall of the level with "
                                 "navigation below to land on. The candidate stands off the wall; its navigation "
                                 "location is the landing.";
        definition.Arguments = Record({ SlotField("center", "Center", "querier"),
                                        FloatField("reach", "Reach", 18.0, 0.0), FloatField("rays", "Rays", 24.0, 0.0),
                                        FloatField("rows", "Rows", 3.0, 0.0),
                                        FloatField("lowest_pitch", "Lowest pitch", 0.0, std::nullopt),
                                        FloatField("highest_pitch", "Highest pitch", 45.0, std::nullopt),
                                        FloatField("eye", "Eye", 1.3, 0.0), FloatField("standoff", "Standoff", 0.55, 0.0),
                                        FloatField("most_slope", "Most slope", 0.3, 0.0), FloatField("drop", "Drop", 12.0, 0.0) });
        definition.Prepare = PrepareSurface;
        definition.Generate = GenerateSurface;
        return definition;
    }
}
