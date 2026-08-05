#pragma once

#include "../SubtypeEditor.h"

#include <memory>

// The movement profile's authoring surface, as one composable unit: the rule
// list form, the resolve preview both movement panels read, and the panels
// themselves. Nothing outside this feature names movement.profile.
[[nodiscard]] std::unique_ptr<IDataSubtypeEditor> CreateMovementProfileEditor();
