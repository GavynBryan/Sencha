#pragma once

#include "EditorComponentAdapter.h"

#include <memory>

// docs/gameplay/held-records.md, "Authoring".
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeHeldEditorAdapter();
