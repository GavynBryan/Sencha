#pragma once
#include <memory>
#include <world/serialization/IComponentSerializer.h>

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeTriggerSerializer();
