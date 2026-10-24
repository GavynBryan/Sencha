#pragma once

#include <ecs/EntityId.h>
#include <physics/components/Collider.h>

#include <optional>
#include <string>

class EditorScene;

// The sensor a VolumeSensor brush cooks to: the box its one piece is, posed in
// its entity's body frame with its scale baked in. Null, with `reason`, when
// the volume is not exactly one box.
[[nodiscard]] std::optional<Collider> CookVolumeSensor(const EditorScene& scene, EntityId entity,
                                                       std::string& reason);
