#pragma once

#include "../DataFormEdit.h"

#include <cstdint>
#include <string>
#include <string_view>

// Whether a slot can take any control or only a button. A composite builds its
// value out of buttons, so offering the mouse's motion in one of its four
// corners would offer a binding the compiler rejects.
enum class InputControlSlotFilter : std::uint8_t
{
    AnyControl,
    ButtonsOnly,
};

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
                                             InputControlSlotFilter filter);
