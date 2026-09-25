#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <string_view>

struct AnimBoundRig;
class World;

// One entity's decision log as a document another machine can read: names,
// not ids. See docs/gameplay/animation.md.

inline constexpr std::string_view kAnimTraceType = "animation.trace";

// Null when the entity carries no decision log. `rig` names content and
// sections; without it they are left as indices.
[[nodiscard]] JsonValue WriteAnimTrace(const World& world, EntityId entity, const AnimBoundRig* rig);
