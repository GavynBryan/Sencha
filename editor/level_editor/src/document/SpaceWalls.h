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
// and placement, and the shelled volumes and openings near it. Zero for
// anything but a shelled space. Never saved: it only tells a rebuild which
// spaces to look at again (EditorScene::CheckedWallInputs).
[[nodiscard]] std::uint32_t SpaceShellInputs(const EditorScene& scene, EntityId space);

// The wall brushes `space` built, generated and kept.
[[nodiscard]] std::vector<EntityId> SpaceWalls(const EditorScene& scene, EntityId space);

// How a space's wall brushes stand against what its shell calls for now: stale
// when a rebuild would change them, and the kept walls the shell has moved on
// from since they were kept.
struct SpaceWallDrift
{
    bool                  Stale = false;
    std::vector<EntityId> KeptBehind;
};
[[nodiscard]] SpaceWallDrift CompareSpaceWalls(const EditorScene& scene, EntityId space, const SpaceShell& shell);
[[nodiscard]] bool SpaceWallsStale(const EditorScene& scene, EntityId space);

// Writes `space`'s walls where they differ from what its shell calls for: each
// slot's brush rewritten while it is still what was built, created where
// missing, deleted where the slot is gone. A brush reshaped by hand becomes
// kept and is left alone, unless `reclaim`, which rebuilds kept slots too.
void RebuildSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder, EntityId space,
                       bool reclaim = false);

// RebuildSpaceWalls for every shelled space whose inputs moved since its walls
// were last checked.
void RebuildStaleSpaceWalls(EditorDocument& document, EntityChangeRecorder& recorder);
