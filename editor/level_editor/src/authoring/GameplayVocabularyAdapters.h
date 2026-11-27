#pragma once

#include "EditorComponentAdapter.h"

//=============================================================================
// Inspectors for the components whose values are registry-resolved names.
//
// In one file because they are one mechanism: each reads a world
// resource that defines the vocabulary, draws rows of names rather than
// numbers, and commits a whole-component value. They share the row widgets and
// they change together. Their editing rules live in GameplayVocabularyEdits.h,
// which is where they are tested.
//
// The generic inspector cannot draw them: a tag container, an attribute set and
// a grant list persist as lists of names, and an entry's count is bounded by its
// item's stack. A single named field it draws itself, as a choice of names.
//=============================================================================

[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeGameplayTagEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeAttributeSetEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeAbilityGrantsEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeItemEntryEditorAdapter();
