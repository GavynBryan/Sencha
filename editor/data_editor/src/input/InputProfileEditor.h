#pragma once

#include "../SubtypeEditor.h"

#include <memory>

// The input profile's authoring surface, with the action vocabulary it binds
// against. Nothing outside this feature names input.profile.
[[nodiscard]] std::unique_ptr<IDataSubtypeEditor> CreateInputProfileEditor();
