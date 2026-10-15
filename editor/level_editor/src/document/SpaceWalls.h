#pragma once

#include "brush/ShellGeneration.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <optional>
#include <vector>

class EditorDocument;
class EditorScene;
class EntityChangeRecorder;

// A space's walls as brushes it writes into the level and keeps up to date
// (docs/plans/level-spatial-model.md, section 5.8).

// What a shelled space's walls are built from, in the world: the shell, the
// openings that cut it (in the order its OpeningRefused problems number them),
// the shelled spaces near it (which ShellSlab::SharedWith indexes), those it
// shares a wall with, and SpaceShellInputs.
struct SpaceShell
{
    ShellResult                     Result;
    std::vector<PersistentEntityId> Openings;
    std::vector<EntityId>           Neighbours;
    std::vector<PersistentEntityId> SharedWith;
    std::uint32_t                   Inputs = 0;
};

// Nullopt for anything but a shelled space.
[[nodiscard]] std::optional<SpaceShell> BuildSpaceShell(const EditorScene& scene, EntityId space);

// The hash of everything a space's walls are built from: its volume, shell
// and placement, the shelled volumes and openings near it, and the open-face
// material. Zero for anything but a shelled space.
[[nodiscard]] std::uint32_t SpaceShellInputs(const EditorScene& scene, EntityId space);

// The wall brushes `space` built, generated and kept.
[[nodiscard]] std::vector<EntityId> SpaceWalls(const EditorScene& scene, EntityId space);

// Whether the space's walls were built from other inputs than it has now.
[[nodiscard]] bool SpaceWallsStale(const EditorScene& scene, EntityId space);

// Writes `space`'s walls: each slot's brush rewritten while it is still what
// was built, created where missing, deleted where the slot is gone. A brush
// edited by hand becomes kept and is left alone, unless `reclaim`, which
// rebuilds kept slots too.
void RebuildSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder, EntityId space,
                       bool reclaim = false);

// RebuildSpaceWalls for every shelled space whose walls are stale.
void RebuildStaleSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder);
