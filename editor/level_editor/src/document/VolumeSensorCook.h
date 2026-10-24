#pragma once

#include <ecs/EntityId.h>
#include <physics/components/Collider.h>

#include <optional>
#include <string>

class EditorScene;

// The sensor collider a VolumeSensor brush cooks to: the box its single piece
// is, posed in its entity's body frame (the entity's world position and turn,
// its scale baked into the box). Null when the volume is not exactly one box,
// with `reason` saying what the author should change.
[[nodiscard]] std::optional<Collider> CookVolumeSensor(const EditorScene& scene, EntityId entity,
                                                       std::string& reason);
