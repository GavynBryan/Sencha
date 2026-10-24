#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The trigger's scene form: its policy fields, and its sequence verbatim as
// authored, so a World that cannot resolve one of its names still saves it.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeTriggerSerializer();
