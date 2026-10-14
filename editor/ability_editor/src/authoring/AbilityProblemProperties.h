#pragma once

#include "properties/PropertySource.h"

class AbilityWorkbench;

// The selected element's problems, and what is under it, each with its one-click
// fix when it has one; fixing is one step.
class AbilityProblemProperties final : public ReadOnlyPropertySource
{
public:
    explicit AbilityProblemProperties(AbilityWorkbench& workbench)
        : Workbench(workbench)
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    AbilityWorkbench& Workbench;
};
