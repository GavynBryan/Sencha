#pragma once

#include "movement/MovementLayerSummary.h"

#include "properties/PropertySource.h"

#include <vector>

class MovementResolvePreview;

// What a movement profile does in a situation the author dials (ground, air,
// immersion, mode, tags, speed): the rules that match and what each
// coefficient comes to.
class MovementResolveProperties final : public CommitOnlyPropertySource
{
public:
    explicit MovementResolveProperties(MovementResolvePreview& preview) : Preview(preview) {}

    // The coefficients' display names and units, from the profile's schema.
    void Label(std::vector<MovementCoefficientLabel> labels) { Labels = std::move(labels); }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override { return Apply(key, value); }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override { return Apply(key, value); }

private:
    PropertyEditResult Apply(std::string_view key, std::string_view value);

    MovementResolvePreview& Preview;
    std::vector<MovementCoefficientLabel> Labels;
};

// How the profile feels, run through the game's own kernels on flat ground:
// how high and long the jump is, how fast the character gets to top speed and
// how far it slides to a stop.
class MovementResponseProperties final : public ReadOnlyPropertySource
{
public:
    explicit MovementResponseProperties(const MovementResolvePreview& preview) : Preview(preview) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    const MovementResolvePreview& Preview;
};
