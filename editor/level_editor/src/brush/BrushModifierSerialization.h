#pragma once

#include "BrushModifier.h"
#include "BrushRecord.h"

#include <core/json/JsonValue.h>

#include <string>

// JSON <-> modifier stack for the brush sidecar (a "modifiers" array on the
// brush's entry). Each entry carries its kind by name; a missing array is an
// empty stack, so documents from before modifiers load unchanged and a brush
// without modifiers still serializes exactly as it did.
[[nodiscard]] JsonValue BrushModifierStackToJson(const BrushModifierStack& stack);

// Entries whose kind is unknown or malformed are skipped and named in `error`
// (appended, one line each); the rest of the stack still loads.
[[nodiscard]] BrushModifierStack BrushModifierStackFromJson(const JsonValue& value,
                                                            std::string* error = nullptr);

// The whole record: the mesh keys plus "modifiers" when the stack is not
// empty. Revision is store metadata and never written.
[[nodiscard]] JsonValue BrushRecordToJson(const BrushRecord& record);
[[nodiscard]] BrushRecord BrushRecordFromJson(const JsonValue& value, std::string* error = nullptr);
