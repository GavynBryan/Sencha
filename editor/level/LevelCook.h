#pragma once

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <math/Vec.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Level cook (docs/plans/sencha-level-editor/05-level-cook.md §5, §6, S6).
// Dev-only — editor-side because it consumes editor brush types (LevelScene /
// BrushTessellate); the engine layer must not depend on the editor, so the
// orchestration lives here while the reusable, brush-agnostic pieces
// (WriteCookedScene, prune, the bake, the clustering) stay in the engine.
//
// Authored level (.json scene + brush_meshes sidecar + default_material) ->
// per-cell Generated .smesh + a cooked scene (one StaticMeshComponent per cell,
// game components passed through) + manifest + AssetId stamping + cooked-cache
// participation. The "one path, two schedulings" PIE counterpart (S7) reuses
// the same collect -> cluster -> bake kernel without touching disk.
//=============================================================================

struct LevelCookResult
{
    bool                     Success = false;
    std::string              Error;
    std::vector<std::string> GeneratedMeshPaths; // asset:// per cell, in cook order
    std::filesystem::path    CookedScenePath;
    std::filesystem::path    ManifestPath;
    std::size_t              CellCount = 0;
};

// Builds the cooked StaticMesh entity JSON for one cell: a Transform at the
// cell origin plus a StaticMesh referencing the cell mesh and its per-section
// materials (bare asset:// paths; WriteCookedScene stamps the ones the id map
// knows). Exposed so the JSON shape can be pinned against the runtime loader.
[[nodiscard]] JsonValue BuildCellEntity(const Vec3d& origin,
                                        std::string_view meshPath,
                                        std::span<const AssetRef> materials);

class LevelDocument;
class LoggingProvider;
struct RuntimeAssets;

// Cooks the authored level at `authoredLevelPath` into `assetsRoot`. cellSize is
// the spatial grid size (a cvar at the call site). Requires
// RegisterLevelSerializers() to have run (the cooked-scene assembly uses the
// scene serializers for passthrough game components). Brush-only: passthrough
// entities carrying asset-handle components (e.g. StaticMesh props) need the
// live overload, which supplies the asset system to resolve their refs.
[[nodiscard]] LevelCookResult CookLevel(const std::filesystem::path& authoredLevelPath,
                                        const std::filesystem::path& assetsRoot,
                                        double cellSize);

// Cooks the live (possibly unsaved) editor document, named `levelName` (the
// artifact stem). The document is snapshotted internally, so the caller's
// document is not modified. This is the editor Cook button's path: author, cook,
// play without a save-to-disk round trip. `assets` is the shared engine asset
// system (the same one the editor authored through and the runtime loads with),
// so passthrough asset-handle components serialize handle->path consistently;
// null cooks brush-only (no prop support), the headless path tests use without
// graphics. `logging` is required (a sink-less provider is fine for headless).
[[nodiscard]] LevelCookResult CookLevel(const LevelDocument& liveDocument,
                                        std::string_view levelName,
                                        const std::filesystem::path& assetsRoot,
                                        double cellSize,
                                        LoggingProvider& logging,
                                        RuntimeAssets* assets = nullptr);
