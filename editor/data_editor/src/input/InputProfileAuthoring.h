#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <memory>
#include <string_view>

[[nodiscard]] std::string_view InputProfileSubtype();

// A profile read as the keymap it describes: contexts holding bindings, each
// titled by what it binds to what, its action chosen among those its action
// set declares, what it is bound to chosen as a shape (one control, two
// buttons as an axis, four directions) that sets its slots, and each slot
// picked from the controls this platform can bind or filled by pressing one.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateInputProfileAuthoring();
