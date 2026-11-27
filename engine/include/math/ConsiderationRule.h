#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <math/ResponseCurve.h>
#include <math/ValueBounds.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// How one measured value bears on a choice, authored the same way by selection
// tables and candidate evaluations: docs/gameplay/selection.md.

enum class ConsiderationMode : std::uint8_t
{
    // Disqualifies when the value lies outside [Min, Max].
    Require,
    // Maps the value through Curve and weights it.
    Score,
};

struct ConsiderationRule
{
    ConsiderationMode Mode = ConsiderationMode::Score;
    ValueBounds Bounds;
    std::optional<ResponseCurve> Curve;
    float Weight = 1.0f;
    // A value that could not be measured disqualifies unless this is set.
    std::optional<float> UnmeasuredScore;

    [[nodiscard]] bool Admits(double value) const { return Bounds.Admits(value); }
    // With no curve the value is taken as already lying in [0, 1].
    [[nodiscard]] float Output(float value) const;
};

// Reads the `require`, `score` and `unmeasured` members of `step`. A score
// without a curve is refused with `missingCurve` unless that is empty.
[[nodiscard]] bool CompileConsiderationRule(const JsonValue& step, std::string_view missingCurve,
                                            ConsiderationRule& out, std::string_view subject,
                                            std::vector<std::string>& errors);

// The schema of the members CompileConsiderationRule reads.
[[nodiscard]] std::vector<DataFieldSchema> MakeConsiderationRuleFields(std::string unmeasuredSummary);
