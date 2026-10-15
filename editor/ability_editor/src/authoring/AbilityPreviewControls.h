#pragma once

#include "properties/PropertySource.h"

#include <string>

class AbilityPreviewSession;

// What the person does to the preview's fixture, each a control the session
// applies on its next tick: press, release, tap or interrupt an input; set an
// attribute; grant or revoke a tag; scale its time; place a target.
class AbilityPreviewControls final : public CommitOnlyPropertySource
{
public:
    explicit AbilityPreviewControls(AbilityPreviewSession& preview)
        : Preview(preview)
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // The attribute the deck plots, chosen on its row; empty for none.
    [[nodiscard]] const std::string& Plotted() const { return PlottedAttribute; }

private:
    AbilityPreviewSession& Preview;
    std::string PlottedAttribute;
};
