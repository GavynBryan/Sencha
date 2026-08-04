#pragma once

#include "../DataFormEdit.h"

#include <string_view>

struct DataSchema;
class DataEditorWorkspace;
class MovementResolvePreview;

// The subtype this form claims. Named rather than spelled at the call site so
// the dispatch and the form cannot drift apart.
[[nodiscard]] std::string_view MovementProfileSubtype();

// Draws a movement profile as an ordered list of rules: each layer shows its
// name, the condition it matches, and what it does to the coefficients, with
// the editing controls under an expander.
//
// `data` is the document's "data" object, mutated in place; the caller owns the
// copy and routes the returned edit through the document's transaction.
[[nodiscard]] FieldEdit DrawMovementProfileForm(JsonValue& data,
                                                const DataSchema& schema,
                                                DataEditorWorkspace& workspace,
                                                MovementResolvePreview& preview);
