#pragma once

#include "EditorComponentAdapter.h"

#include <memory>

// docs/gameplay/equipment.md, "Authoring".
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeEquipmentStateEditorAdapter();
