#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

class JsonValue;

//=============================================================================
// Shared cooked-scene output (docs/assets/pipeline.md Decisions A/D;
// docs/plans/sencha-level-editor/05-level-cook.md §5). Dev-only
// (SENCHA_ENABLE_COOK). The manifest + id-map + scene-stamp dance that every
// scene cook performs, factored out of the proto-cook so both the CubeDemo
// generator and the level cook share one implementation.
//
// Given an already-assembled cooked scene, this:
//   1. Collects every asset:// ref in the scene, then walks one level of .smat
//      indirection (a referenced material's own texture refs), plus any
//      `extraRefs` the caller knows are real but do not appear as strings in
//      the scene JSON (the brush-sidecar material refs, 05-§5 step 4).
//   2. Maintains the persisted AssetIdMap: first-sight mint, rename inheritance
//      by content hash. A broken existing map is an error, never a silent
//      re-mint (that would lose rename history).
//   3. Writes the manifest and the id-stamped cooked scene.
//
// asset:// -> physical resolution is a caller seam (`physicalPathFor`): the
// CubeDemo maps asset://x to root/x, but the level cook's Generated meshes live
// under .cooked/, so the mapping is not one rule. Used for content hashing and
// liveness only.
//=============================================================================
[[nodiscard]] bool WriteCookedScene(
    const JsonValue& cookedScene,
    std::span<const std::string> extraRefs,
    const std::function<std::filesystem::path(std::string_view)>& physicalPathFor,
    const std::filesystem::path& idMapPath,
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& cookedScenePath,
    std::string* error = nullptr);
