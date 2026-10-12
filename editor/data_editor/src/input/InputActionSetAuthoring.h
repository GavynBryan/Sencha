#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <memory>

// An action set read as the vocabulary it declares: each action titled by its
// name and the shape of value it carries, its firing mode shown only for a
// button, and a change of shape taking the mode with it.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateInputActionSetAuthoring();
