#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <memory>

// A profile read as the keymap it describes: bindings titled by what they bind,
// actions chosen among the declared ones, a shape setting a binding's slots,
// and a slot picked or filled by pressing a control.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateInputProfileAuthoring();
