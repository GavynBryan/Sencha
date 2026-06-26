#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>

class CookedCacheIndex;

//=============================================================================
// Cooked-artifact prune (docs/plans/sencha-level-editor/05-level-cook.md §6).
// Dev-only (SENCHA_ENABLE_COOK).
//
// The "no garbage" guarantee: a deleted source must not leave its cooked
// artifacts, cache entry, and ids lingering. The source-keyed cache is the
// single source of truth for what a source produced, so pruning is: for every
// source the cache records that is no longer live, delete its artifact files
// (always under .cooked/, recorded as FileRelPath) and drop its entry.
//
// Liveness is a caller seam, not a baked-in rule: the default is "the source
// file still exists under assetsRoot", but the level cook passes a predicate
// that also reports a level with no brushes as dead (§6). The index never
// touches the filesystem itself; this function owns the deletes.
//=============================================================================

// Returns the number of source entries pruned. `sourceIsLive` receives the
// assets-root-relative source path; null means the default existence check.
[[nodiscard]] std::size_t PruneOrphanedGeneratedArtifacts(
    CookedCacheIndex& index,
    const std::filesystem::path& assetsRoot,
    const std::function<bool(std::string_view sourceRelPath)>& sourceIsLive = nullptr);
