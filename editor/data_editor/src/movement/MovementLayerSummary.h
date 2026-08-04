#pragma once

#include <core/metadata/DataSchema.h>
#include <movement/MovementProfileData.h>

#include <string>
#include <string_view>
#include <vector>

// Reads a compiled movement layer back as prose, so an ordered list of layers
// presents as the ruleset the runtime actually evaluates rather than as JSON.
//
// Pure text synthesis: no ImGui, no document, no engine state. Everything here
// is table-tested, which is also why the exact wording is deliberate — changing
// a phrase is a test update, not an accident.
//
// Text is ASCII on purpose. The editor fonts declare no glyph ranges beyond
// Latin-1, so arrows and typographic operators would not render.

// Display metadata for one authored coefficient, lifted from the registered
// schema so wording and units are never restated here.
struct MovementCoefficientLabel
{
    std::string Key;
    std::string Display;
    std::string Units;
};

// Pulls the eight coefficient labels out of a movement profile schema. Returns
// empty if the schema does not have the expected layer/patch shape.
[[nodiscard]] std::vector<MovementCoefficientLabel> MovementCoefficientLabels(
    const DataSchema& schema);

// "When in the air and while movement.jump.requested", or "Always" when the
// layer carries no conditions.
[[nodiscard]] std::string DescribeMovementCondition(const MovementLayerCondition& condition);

// The condition rendered as a label rather than a sentence: "In the air",
// "Always". Used when a layer has no authored name.
[[nodiscard]] std::string SynthesizeMovementLayerName(const MovementLayerCondition& condition);

// The authored name when present, otherwise the synthesized one.
[[nodiscard]] std::string MovementLayerName(const MovementProfileLayer& layer);

// "Friction = 0, Wish speed cap = 0.76 m/s" across set, scale, and add in the
// order the runtime applies them. Empty when the layer changes nothing.
[[nodiscard]] std::string DescribeMovementPatches(
    const MovementProfileLayer& layer,
    const std::vector<MovementCoefficientLabel>& labels);

// Trims trailing zeros so tuned values read as authored: "0", "4.5", "0.76".
[[nodiscard]] std::string FormatMovementCoefficient(float value);
