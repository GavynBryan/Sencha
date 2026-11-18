#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeEquipmentStateSerializer();
