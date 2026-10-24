#pragma once

#include "authoring/EditorComponentAdapter.h"

#include <memory>

// A trigger's sequence edited below its policy fields, each change one
// undoable step; and in the view, its name over the volume and an arrow for
// every direction its commands are given.
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeTriggerEditorAdapter();
