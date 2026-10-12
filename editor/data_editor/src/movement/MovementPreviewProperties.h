#pragma once

#include "movement/MovementLayerSummary.h"

#include "properties/PropertySource.h"

#include <vector>

class MovementResolvePreview;

// What a movement profile does in a situation the author dials (ground, air,
// immersion, mode, tags, speed): the rules that match and what each
// coefficient comes to.
class MovementResolveProperties final : public IPropertySource
{
public:
    explicit MovementResolveProperties(MovementResolvePreview& preview) : Preview(preview) {}

    // The coefficients' display names and units, from the profile's schema.
    void Label(std::vector<MovementCoefficientLabel> labels) { Labels = std::move(labels); }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override { return Apply(key, value); }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override { return Apply(key, value); }
    void CancelEdit(std::string_view) override {}

private:
    PropertyEditResult Apply(std::string_view key, std::string_view value);

    MovementResolvePreview& Preview;
    std::vector<MovementCoefficientLabel> Labels;
};

// How the profile feels, run through the game's own kernels on flat ground:
// how high and long the jump is, how fast the character gets to top speed and
// how far it slides to a stop.
class MovementResponseProperties final : public IPropertySource
{
public:
    explicit MovementResponseProperties(const MovementResolvePreview& preview) : Preview(preview) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}

private:
    const MovementResolvePreview& Preview;
};
