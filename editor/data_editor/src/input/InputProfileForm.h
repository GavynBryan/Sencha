#pragma once

#include "../DataFormEdit.h"

#include <string_view>

struct DataSchema;
class DataEditorWorkspace;
class InputControlCapture;
class InputProfilePreview;

// The subtype this form claims. Named rather than spelled at the call site so
// the registration and the form cannot drift apart.
[[nodiscard]] std::string_view InputProfileSubtype();

// Draws a profile as the keymap it describes: contexts holding bindings, each
// binding reading as the control it is on.
//
// `data` is the document's "data" object, mutated in place; the caller owns the
// copy and routes the returned edit through the document's transaction.
[[nodiscard]] FieldEdit DrawInputProfileForm(JsonValue& data,
                                             const DataSchema& schema,
                                             DataEditorWorkspace& workspace,
                                             InputProfilePreview& preview,
                                             InputControlCapture& capture);
