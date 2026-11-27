#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// An entry's count is clamped to its stack as it loads, so its scene form is hand-written.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemEntrySerializer();
// The generated forms, with no default record: a container or a bare pickup at its defaults is still one.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemContainerSerializer();
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemPickupSerializer();
