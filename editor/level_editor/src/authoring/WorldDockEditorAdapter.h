#pragma once

#include "EditorComponentAdapter.h"

[[nodiscard]] std::unique_ptr<IEditorComponentAdapter>
MakeWorldDockEditorAdapter();

