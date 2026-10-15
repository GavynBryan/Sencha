#pragma once

#include "properties/PropertySource.h"

#include <core/json/JsonValue.h>

#include <functional>

class AbilityPreviewSession;

// The preview's tick as the ability pass left it: each activation the fixture
// holds -- its phase and how long it has been there, the pulse, leases held,
// groups paid, cooldown, why it ended -- and every event the pass traced, its
// elements named from the working data. Read from the trace and the records.
class AbilityPreviewInspector final : public ReadOnlyPropertySource
{
public:
    AbilityPreviewInspector(AbilityPreviewSession& preview, std::function<const JsonValue*()> data)
        : Preview(preview)
        , Data(std::move(data))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;

private:
    AbilityPreviewSession& Preview;
    std::function<const JsonValue*()> Data;
};
