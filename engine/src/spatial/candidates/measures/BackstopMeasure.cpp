#include "../CandidateOperationSupport.h"

#include <physics/LevelGeometry.h>

#include <array>

namespace candidate_operation
{
    namespace
    {
        // How far short of the point a hit still counts as beyond it: a point on
        // a wall is its own backstop.
        constexpr float kBeyondMargin = 0.3f;

        struct BackstopState
        {
            std::uint8_t Slot = 0;
            float Reach = 18.0f;
            float Lift = 1.3f;
        };

        CandidatePrepared PrepareBackstop(const JsonValue& arguments, const CandidatePrepareContext& context,
                                          std::string_view subject, std::vector<std::string>& errors)
        {
            BackstopState state;
            state.Slot = ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0);
            state.Reach = FloatOr(arguments, "reach", state.Reach);
            state.Lift = FloatOr(arguments, "lift", state.Lift);
            if (!(state.Reach > 0.0f))
                errors.push_back(std::string(subject) + ": reach is positive");
            return Prepared(state);
        }

        void MeasureBackstop(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                             std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& backstop = *static_cast<const BackstopState*>(state);
            const PhysicsQueries* physics = run.Physics();
            if (physics == nullptr || run.SlotSize(backstop.Slot) == 0)
            {
                for (const std::uint32_t row : rows)
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                return;
            }
            const Vec3d point = run.SlotPoint(backstop.Slot, 0).Position;
            const std::array<EntityId, 1> ignored{ run.Querier() };
            const PhysicsQueryFilter filter{ .IncludeTriggers = false, .IgnoreEntities = ignored, .IncludeCharacters = false };
            for (const std::uint32_t row : rows)
            {
                const Vec3d from = run.Position(row) + Vec3d::Up() * backstop.Lift;
                const Vec3d toward = point - from;
                const float length = toward.Magnitude();
                if (length < 0.01f)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                if (!run.ChargeRaycast())
                {
                    statuses[row] = CandidateMeasureStatus::BeyondBudget;
                    continue;
                }
                const RaycastHit hit = physics->Raycast(from, toward / length, backstop.Reach, filter);
                statuses[row] = CandidateMeasureStatus::Ok;
                values[row] = hit.Hit && hit.Distance >= length - kBeyondMargin && IsLevelGeometry(run.Entities(), hit.Entity)
                    ? 1.0f
                    : 0.0f;
            }
        }
    }

    CandidateMeasureDefinition BackstopMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.backstop";
        definition.DisplayName = "Backstop";
        definition.Description = "1 when a straight line from the candidate through a slot's first point goes on to "
                                 "meet the level within the reach; 0 when it meets something first or nothing at all.";
        definition.Arguments = Record({ SlotField("slot", "Through"), FloatField("reach", "Reach", 18.0, 0.0),
                                        FloatField("lift", "Lift", 1.3, 0.0) });
        definition.Cost = CandidateCostClass::Geometric;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Prepare = PrepareBackstop;
        definition.Measure = MeasureBackstop;
        return definition;
    }
}
