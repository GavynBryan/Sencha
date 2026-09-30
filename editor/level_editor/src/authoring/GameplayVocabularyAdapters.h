#pragma once

#include "EditorComponentAdapter.h"

//=============================================================================
// Inspectors for the components whose values are registry-resolved names.
//
// Four of them, in one file, because they are one mechanism: each reads a world
// resource that defines the vocabulary, draws rows of names rather than
// numbers, and commits a whole-component value. They share the row widgets and
// they change together. Their editing rules live in GameplayVocabularyEdits.h,
// which is where they are tested.
//
// The generic inspector cannot draw any of them: their persisted form is a list
// of names, so their serializers describe no byte-addressable fields and a
// registration-order id is not something a designer can be shown a number for.
//=============================================================================

[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeGameplayTagEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeAttributeSetEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeAbilitySetEditorAdapter();
[[nodiscard]] std::unique_ptr<IEditorComponentAdapter> MakeCharacterMovementEditorAdapter();
