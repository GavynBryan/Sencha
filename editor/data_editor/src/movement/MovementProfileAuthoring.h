#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <memory>

// A movement profile read as the ruleset the runtime evaluates: rules titled
// and marked live in a dialled situation, with what they resolve to beside.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateMovementProfileAuthoring();
