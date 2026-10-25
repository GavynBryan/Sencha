#pragma once

#include "authoring/EditorComponentAdapter.h"

#include <memory>

// An entity's responses, each binding edited as a card: the occurrence it
// answers, its policy, and its sequence, each change one undoable step. On a
// volume it is a level trigger: headed Trigger, its name over the volume and an
// arrow for every direction its commands are given. On anything else it is the
// entity's Behaviors, and draws nothing in the view.
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeResponsesEditorAdapter();
