#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <memory>

// A movement profile read as the ruleset the runtime evaluates: each rule
// titled by its name or condition and marked active or off in the situation
// the author dials, its tag conditions picked among the tags the profile
// knows, and beside it what the rules resolve to and how the profile feels.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateMovementProfileAuthoring();
