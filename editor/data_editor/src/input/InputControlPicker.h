#pragma once

#include "../DataFormEdit.h"
#include "input/InputControlCapture.h"
#include "input/InputSlotAcceptance.h"

#include <cstdint>
#include <string>
#include <string_view>

// One control slot of a binding: the authored name, a picker listing what this
// platform can actually bind, and whatever the slot's owner adds beside them.
//
// The text stays editable. A picker makes the vocabulary discoverable; it
// should not make a name the author already knows harder to type, and a name
// this build does not recognize has to remain visible to be fixed.
[[nodiscard]] FieldEdit DrawInputControlSlot(JsonValue& binding,
                                             std::string_view key,
                                             const char* label,
                                             const std::string& fieldPath,
                                             InputSlotAcceptance acceptance,
                                             InputControlCapture& capture,
                                             std::string_view documentPath,
                                             std::uint64_t documentRevision);
