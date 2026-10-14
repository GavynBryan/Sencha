#include "../CandidateOperationSupport.h"

#include <cmath>
#include <numbers>

namespace candidate_operation
{
    namespace
    {
        struct RingState
        {
            std::uint8_t Center = 0;
            std::uint32_t Rings = 1;
            std::uint32_t PointsPerRing = 8;
            float InnerRadius = 0.0f;
            float OuterRadius = 0.0f;
            float AngleOffset = 0.0f;
            Projection Projected;
        };

        CandidatePrepared PrepareRing(const JsonValue& arguments, const CandidatePrepareContext& context,
                                      std::string_view subject, std::vector<std::string>& errors)
        {
            RingState state;
            state.Center = ReadSlot(arguments, "center", "querier", context, subject, errors).value_or(0);
            state.Rings = static_cast<std::uint32_t>(arguments.NumberOr("rings", 1));
            state.PointsPerRing = static_cast<std::uint32_t>(arguments.NumberOr("points_per_ring", 8));
            state.OuterRadius = FloatOr(arguments, "outer_radius", 0.0f);
            state.InnerRadius = FloatOr(arguments, "inner_radius", state.OuterRadius);
            state.AngleOffset = FloatOr(arguments, "angle_offset_degrees", 0.0f) * std::numbers::pi_v<float> / 180.0f;
            state.Projected = ReadProjection(arguments);
            if (state.InnerRadius > state.OuterRadius)
                errors.push_back(std::string(subject) + ": inner_radius exceeds outer_radius");
            return Prepared(state);
        }

        // Ring by ring from the inside out, each counter-clockwise from the offset.
        void GenerateRing(CandidateRun& run, const void* state)
        {
            const auto& ring = *static_cast<const RingState*>(state);
            if (run.SlotSize(ring.Center) == 0)
                return;
            const Vec3d center = run.SlotPoint(ring.Center, 0).Position;
            for (std::uint32_t index = 0; index < ring.Rings; ++index)
            {
                const float t = ring.Rings > 1 ? static_cast<float>(index) / static_cast<float>(ring.Rings - 1) : 1.0f;
                const float radius = ring.InnerRadius + (ring.OuterRadius - ring.InnerRadius) * t;
                for (std::uint32_t point = 0; point < ring.PointsPerRing; ++point)
                {
                    const float angle = ring.AngleOffset
                        + 2.0f * std::numbers::pi_v<float> * static_cast<float>(point)
                            / static_cast<float>(ring.PointsPerRing);
                    const Vec3d sample = center + Vec3d(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                    if (!AppendSample(run, sample, ring.Projected))
                        return;
                }
            }
        }
    }

    CandidateGeneratorDefinition RingGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.ring";
        definition.DisplayName = "Rings";
        definition.Description = "Points on concentric rings around a slot, optionally projected onto the "
                                 "querier's navigation.";
        definition.Arguments = Record({ SlotField("center", "Center", "querier"),
                                        IntField("rings", "Rings", 1, 1),
                                        IntField("points_per_ring", "Points per ring", 8, 1),
                                        FloatField("inner_radius", "Inner radius", std::nullopt, 0.0),
                                        FloatField("outer_radius", "Outer radius", std::nullopt, 0.0),
                                        FloatField("angle_offset_degrees", "Angle offset (degrees)", 0.0) });
        definition.Arguments.Children[3].Required = false;
        for (DataFieldSchema& field : ProjectionFields())
            definition.Arguments.Children.push_back(std::move(field));
        definition.Prepare = PrepareRing;
        definition.Generate = GenerateRing;
        return definition;
    }
}
