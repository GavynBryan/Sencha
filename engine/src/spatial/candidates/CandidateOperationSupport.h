#pragma once

#include <spatial/candidates/CandidateCatalogs.h>
#include <spatial/candidates/CandidateRun.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagQuery.h>

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
