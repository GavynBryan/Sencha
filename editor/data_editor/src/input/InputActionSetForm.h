#pragma once

#include "../DataFormEdit.h"

#include <string_view>

struct DataSchema;
class DataEditorWorkspace;

// The subtype this form claims. Named rather than spelled at the call site so
// the registration and the form cannot drift apart.
[[nodiscard]] std::string_view InputActionSetSubtype();

// Draws an action set as the vocabulary it is: one row per action, saying what
// shape it carries and whether the simulation sees it.
//
// `data` is the document's "data" object, mutated in place; the caller owns the
// copy and routes the returned edit through the document's transaction.
[[nodiscard]] FieldEdit DrawInputActionSetForm(JsonValue& data,
                                               const DataSchema& schema,
                                               DataEditorWorkspace& workspace);
