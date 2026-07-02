#pragma once

#include "commands/ICommand.h"

#include <ecs/EntityId.h>

#include <filesystem>
#include <memory>

class EditorDocument;
class EditorScene;
class AssetSystem;
class AssetRegistry;
class LoggingProvider;

// Bakes the brush entity's polygon mesh to a .smesh asset under the project
// content root and swaps its BrushComponent for a StaticMeshComponent that
// references it. Reversible twice over: Undo restores the brush exactly (the
// source mesh stays dormant in the BrushMeshStore under BakedBrushComponent),
// and "Revert to Brush" undoes a bake from an earlier session. The written
// .smesh stays on disk on undo (a content-hashed orphan; a later re-bake of
// the same geometry reuses it). Null when the entity has no brush, the bake
// yields no geometry, or the file cannot be written.
[[nodiscard]] std::unique_ptr<ICommand> MakeBakeBrushToMeshCommand(
    EditorScene& scene,
    EditorDocument& document,
    AssetSystem& assets,
    AssetRegistry& catalog,
    LoggingProvider& logging,
    EntityId entity,
    const std::filesystem::path& contentRoot);

// The inverse swap: StaticMeshComponent (+ dormant source) back to a live
// BrushComponent. Null when the entity has no baked-brush annotation.
[[nodiscard]] std::unique_ptr<ICommand> MakeRevertBakedBrushCommand(
    EditorScene& scene,
    EditorDocument& document,
    AssetSystem& assets,
    EntityId entity);
