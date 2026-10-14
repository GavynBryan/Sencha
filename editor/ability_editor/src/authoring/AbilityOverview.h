#pragma once

#include "properties/PropertySource.h"

class AbilityWorkbench;

// The open ability at a glance: the binding it gets with nothing overriding it,
// when it may start, continue and is cancelled, what it costs, its cooldown, how
// it is predicted, and how many problems it has.
class AbilityOverviewProperties final : public ReadOnlyPropertySource
{
public:
    explicit AbilityOverviewProperties(AbilityWorkbench& workbench)
        : Workbench(workbench)
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    AbilityWorkbench& Workbench;
};
