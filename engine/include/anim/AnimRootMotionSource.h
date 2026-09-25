#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

class World;
struct RootMotionSample;

//=============================================================================
// Animation's answer to RootMotionSource
//
// An entity is carried while its base layer plays a behavior flagged
// root_motion whose clip has a root curve. The motion on a tick is the curve
// between the content times of that tick and the one before, turned into the
// character's facing.
//
// On a request-keyed base layer what played on a tick follows from the
// request records alone -- each carries its start and cancel ticks -- so a
// replayed tick is carried as the authority carried it, through a cancel or a
// corrected start. A selector's base layer is carried by what it plays now:
// selection is state, and a replay across a selection change is carried by
// the newer choice.
//
// While carried the character goes nowhere else across the ground, even when
// the curve is still: a mantle's first frames hold it in place.
//=============================================================================

[[nodiscard]] bool SampleAnimRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds,
                                        RootMotionSample& out);
