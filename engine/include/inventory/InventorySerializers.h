#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemEntrySerializer();
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemContainerSerializer();
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeItemPickupSerializer();
