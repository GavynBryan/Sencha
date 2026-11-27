#pragma once

#include "EditorComponentAdapter.h"

#include <memory>

[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeEquipmentStateEditorAdapter();
