#pragma once

#include <spatial/candidates/CandidateCatalogs.h>
#include <spatial/candidates/CandidateRun.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagQuery.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shared by the engine's candidate operations: argument fields, argument
// readers, the tag-query argument, and sample projection.

namespace candidate_operation
{
    inline const Vec3d kUp(0.0f, 1.0f, 0.0f);

    // -- Argument fields ------------------------------------------------------
    [[nodiscard]] DataFieldSchema SlotField(std::string key, std::string displayName, std::string fallback = {});
    [[nodiscard]] DataFieldSchema FloatField(std::string key, std::string displayName, std::optional<double> fallback,
                                             std::optional<double> minimum = std::nullopt);
    [[nodiscard]] DataFieldSchema IntField(std::string key, std::string displayName, std::int64_t fallback,
                                           std::int64_t minimum);
    [[nodiscard]] DataFieldSchema BoolField(std::string key, std::string displayName, bool fallback);
    [[nodiscard]] DataFieldSchema EnumField(std::string key, std::string displayName,
                                            std::initializer_list<std::string_view> choices, std::string_view fallback);
    [[nodiscard]] DataFieldSchema Record(std::initializer_list<DataFieldSchema> fields);

    // -- Argument readers -----------------------------------------------------
    [[nodiscard]] std::string_view StringOr(const JsonValue& arguments, std::string_view key, std::string_view fallback);
    [[nodiscard]] bool BoolOr(const JsonValue& arguments, std::string_view key, bool fallback);
    [[nodiscard]] float FloatOr(const JsonValue& arguments, std::string_view key, float fallback);

    // Nullopt, with an error, for a name the definition does not declare.
    [[nodiscard]] std::optional<std::uint8_t> ReadSlot(const JsonValue& arguments,
                                                       std::string_view key,
                                                       std::string_view fallback,
                                                       const CandidatePrepareContext& context,
                                                       std::string_view subject,
                                                       std::vector<std::string>& errors);

    template<typename State>
    [[nodiscard]] CandidatePrepared Prepared(State state, std::optional<CandidateCostClass> cost = std::nullopt)
    {
        return CandidatePrepared{ std::make_shared<const State>(std::move(state)), cost };
    }

    // -- Reduction over a slot's points ---------------------------------------
    enum class Reduce : std::uint8_t
    {
        Min,
        Max,
    };
    [[nodiscard]] DataFieldSchema ReduceField();
    [[nodiscard]] Reduce ReadReduce(const JsonValue& arguments);

    // -- Tag queries ----------------------------------------------------------
    struct TagQueryNames
    {
        std::vector<std::string> All;
        std::vector<std::string> Any;
        std::vector<std::string> None;
        GameplayTagMatchMode Match = GameplayTagMatchMode::Exact;

        [[nodiscard]] bool Empty() const { return All.empty() && Any.empty() && None.empty(); }
    };
    [[nodiscard]] DataFieldSchema TagQueryField(std::string key, std::string displayName);
    [[nodiscard]] TagQueryNames ReadTagQuery(const JsonValue& arguments, std::string_view key);
    // False, with one error per unknown name, when a tag is not registered.
    [[nodiscard]] bool BindTagQuery(const TagQueryNames& names,
                                    const CandidateBindEnvironment& environment,
                                    std::string_view subject,
                                    std::vector<std::string>& errors,
                                    GameplayTagQuery& out);

    // -- Sample projection ----------------------------------------------------
    struct Projection
    {
        bool Project = true;
        float Height = 2.0f;
        bool KeepUnprojected = false;
    };
    [[nodiscard]] std::vector<DataFieldSchema> ProjectionFields();
    [[nodiscard]] Projection ReadProjection(const JsonValue& arguments);
    // Appends the point, projected onto the querier's navigation when asked and
    // available. False once the set is full.
    bool AppendSample(CandidateRun& run, const Vec3d& point, const Projection& projection);

    // -- Sampled generators ---------------------------------------------------
    // Steps from a grid's center along an axis, or rings and points per ring:
    // generous for any authored layout, and small enough to index with an int.
    inline constexpr double kMaxGeneratorSteps = 65536.0;

    // The number at `key`, or an error when it is not a finite float.
    [[nodiscard]] float FiniteFloatOr(const JsonValue& arguments, std::string_view key, float fallback,
                                      std::string_view subject, std::vector<std::string>& errors);
    // A whole number in [1, kMaxGeneratorSteps], or an error.
    [[nodiscard]] std::uint32_t StepCountOr(const JsonValue& arguments, std::string_view key, std::uint32_t fallback,
                                            std::string_view subject, std::vector<std::string>& errors);

    struct GridExtent
    {
        float Radius = 0.0f;
        float Spacing = 1.0f;
        int Steps = 0;
    };
    // "radius" and "spacing": finite, spacing positive, and at most
    // kMaxGeneratorSteps spacings from the center to the rim.
    [[nodiscard]] GridExtent ReadGridExtent(const JsonValue& arguments, std::string_view subject,
                                            std::vector<std::string>& errors);

    // Every grid offset within the radius, row-major from -x -z, each charged
    // as a generation attempt. Stops when attempts run out or `visit` says so.
    template<typename Visit>
    void ForEachGridOffset(CandidateRun& run, const GridExtent& grid, Visit&& visit)
    {
        const float radiusSquared = grid.Radius * grid.Radius;
        const auto inside = [&](int x, float dz) {
            const float dx = static_cast<float>(x) * grid.Spacing;
            return dx * dx + dz * dz <= radiusSquared;
        };
        for (int z = -grid.Steps; z <= grid.Steps; ++z)
        {
            const float dz = static_cast<float>(z) * grid.Spacing;
            if (!inside(0, dz))
                continue;
            // The row's half-chord in closed form, nudged to agree exactly with
            // `inside`, so the walk costs what it visits.
            const float chord = std::sqrt(std::max(radiusSquared - dz * dz, 0.0f)) / grid.Spacing;
            int reach = std::min(static_cast<int>(chord), grid.Steps);
            while (reach > 0 && !inside(reach, dz))
                --reach;
            while (reach < grid.Steps && inside(reach + 1, dz))
                ++reach;
            for (int x = -reach; x <= reach; ++x)
            {
                if (!run.ChargeGenerationAttempt())
                    return;
                if (!visit(Vec3d(static_cast<float>(x) * grid.Spacing, 0.0f, dz)))
                    return;
            }
        }
    }

    // -- Navigation measures --------------------------------------------------
    enum class OutsideZone : std::uint8_t
    {
        Reject,
        Estimate,
    };
    [[nodiscard]] DataFieldSchema OutsideZoneField();
    [[nodiscard]] OutsideZone ReadOutsideZone(const JsonValue& arguments);

    // The operations the engine declares, one per source file.
    [[nodiscard]] CandidateGeneratorDefinition PointsGenerator();
    [[nodiscard]] CandidateGeneratorDefinition EntitiesGenerator();
    [[nodiscard]] CandidateGeneratorDefinition RingGenerator();
    [[nodiscard]] CandidateGeneratorDefinition GridGenerator();
    [[nodiscard]] CandidateGeneratorDefinition ReachableGenerator();
    [[nodiscard]] CandidateMeasureDefinition DistanceMeasure();
    [[nodiscard]] CandidateMeasureDefinition HeightMeasure();
    [[nodiscard]] CandidateMeasureDefinition FacingMeasure();
    [[nodiscard]] CandidateMeasureDefinition EntityTagsMeasure();
    [[nodiscard]] CandidateMeasureDefinition ReachableMeasure();
    [[nodiscard]] CandidateMeasureDefinition TravelCostMeasure();
    [[nodiscard]] CandidateMeasureDefinition VisibilityMeasure();
    [[nodiscard]] CandidateMeasureDefinition RouteVisibilityMeasure();
    [[nodiscard]] CandidateMeasureDefinition AuthoredQueryMeasure();
}
