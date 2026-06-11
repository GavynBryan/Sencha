#pragma once

// Convenience umbrella header — includes the full public ECS API.
// Engine modules may include this header for complete ECS access,
// or include individual headers for faster incremental builds.

#include <ecs/ArchetypeSignature.h>
#include <ecs/Chunk.h>
#include <ecs/CommandBuffer.h>
#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/EntityRegistry.h>
#include <ecs/Query.h>
#include <ecs/QueryAccessors.h>
#include <ecs/World.h>
