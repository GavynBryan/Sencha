#pragma once

#include <world/scene/SmapFormat.h>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

class ComponentSerializerRegistry;
class JsonValue;

//=============================================================================
// Shared cooked-scene output (docs/assets/pipeline.md Decisions A/D;
// docs/plans/sencha-level-editor/05-level-cook.md §5). Dev-only
// (SENCHA_ENABLE_COOK). The dependency + id-map + scene-compile dance every
// scene cook performs, shared by the CubeDemo generator and the level cook.
//
// Given an already-assembled cooked scene, this:
//   1. Collects every asset:// ref in the scene, then walks one level of .smat
//      indirection (a referenced material's own texture refs), plus any
//      `extraRefs` the caller knows are real but do not appear as strings in
//      the scene JSON (the brush-sidecar material refs, 05-§5 step 4).
//   2. Maintains the persisted AssetIdMap: first-sight mint, rename inheritance
//      by content hash. A broken existing map is an error, never a silent
//      re-mint (that would lose rename history).
//   3. Compiles the id-stamped scene, the dependency table, and the collision
//      cells into one .smap image and writes it. The runtime reads that file
//      and nothing else: no manifest sidecar, no collision sidecar.
//
// asset:// -> physical resolution is a caller seam (`physicalPathFor`): the
// CubeDemo maps asset://x to root/x, but the level cook's Generated meshes live
// under .cooked/, so the mapping is not one rule. Used for content hashing and
// liveness only.
//=============================================================================
[[nodiscard]] bool WriteCookedScene(
    const JsonValue& cookedScene,
    std::span<const std::string> extraRefs,
    std::span<const SmapCollisionCell> collisionCells,
    const ComponentSerializerRegistry& serializers,
    const std::function<std::filesystem::path(std::string_view)>& physicalPathFor,
    const std::filesystem::path& idMapPath,
    const std::filesystem::path& cookedScenePath,
    std::string* error = nullptr);
