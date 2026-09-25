#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

class World;
struct RootMotionSample;

// The base layer's root curve between this tick's and the previous tick's content
// times, in the character's facing. See docs/gameplay/animation.md.
[[nodiscard]] bool SampleAnimRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                                        RootMotionSample& out);
