#pragma once

#include "../SubtypeEditor.h"

#include <memory>

// The action set's authoring surface. Nothing outside this feature names
// input.actions.
[[nodiscard]] std::unique_ptr<IDataSubtypeEditor> CreateInputActionSetEditor();
