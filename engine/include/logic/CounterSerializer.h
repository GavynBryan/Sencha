#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// A counter persists its name as the tag's name, which a TypeSchema cannot
// state: the component holds a registration-order tag id.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeCounterSerializer();
