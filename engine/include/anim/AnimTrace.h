#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <string_view>

struct AnimBoundRig;
class World;

//=============================================================================
// Animation traces
//
// What one entity's decision log holds, as a document a bug report can carry
// to another machine: every record with its tick, cause and reason, and names
// resolved against this World -- behaviors, intents, layers, content,
// sections -- since ids mean nothing elsewhere. A trace carries decisions
// only; it has no pose history and says so.
//=============================================================================

inline constexpr std::string_view kAnimTraceType = "animation.trace";

// Null when the entity carries no decision log. `rig` names content and
// sections; without it they are left as indices.
[[nodiscard]] JsonValue WriteAnimTrace(const World& world, EntityId entity, const AnimBoundRig* rig);
