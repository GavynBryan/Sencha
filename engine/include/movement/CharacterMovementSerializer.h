#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The generated form of CharacterMovement, less a mode a LocomotionModeLease holds:
// docs/gameplay/saves.md, "Characters and actors".
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeCharacterMovementSerializer();
