#pragma once

#include "EditorComponentAdapter.h"

// The inspector rows for BrushComponent: the brush's modifier stack, as an
// ordered list a designer adds to, reorders, enables, edits, and removes, every
// change one undoable stack edit. Inspector-only; the mirror planes draw from
// the selection renderer.
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeBrushModifierEditorAdapter();
