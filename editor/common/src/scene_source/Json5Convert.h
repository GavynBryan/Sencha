#pragma once

#include "scene_source/Json5Value.h"

#include <core/json/JsonValue.h>

// Structural conversion between the engine's strict JSON values and scene
// source values. Trivia does not cross: JsonValue cannot hold it, so
// Json5FromJson produces uncommented values and Json5ToJson drops comments.
// The document bridge composes these with its trivia-carrying merge, which is
// where comments survive a save.
[[nodiscard]] Json5Value Json5FromJson(const JsonValue& value);
[[nodiscard]] JsonValue Json5ToJson(const Json5Value& value);
