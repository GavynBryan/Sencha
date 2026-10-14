#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <spatial/sight/SightComponents.h>
#include <spatial/sight/SightTest.h>

#include <optional>

// A positive finite range, a half-angle in (0, pi], and a finite eye offset.
[[nodiscard]] bool IsUsableSightSensor(const SightSensor& sensor);

// The sensor's eye in the world, facing the entity's forward axis. Nullopt when
// the sensor is not usable, so a bad authoring never sees.
[[nodiscard]] std::optional<SightObserver> PlaceSightObserver(const SightSensor& sensor, const Transform3f& world);

[[nodiscard]] Vec3d PlaceSightTarget(const SightTarget& target, const Transform3f& world);
