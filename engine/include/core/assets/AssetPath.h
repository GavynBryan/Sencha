#pragma once

#include <string_view>

//=============================================================================
// AssetPath
//
// The virtual asset-path vocabulary, dependency-neutral on purpose: it is a
// pure string predicate with no knowledge of the registry, caches, or any
// asset machinery. Low-level data validators (mesh, animation) need to check
// that a stored skeleton/texture reference is a well-formed "asset://" path
// without taking a dependency on AssetRegistry's shape — so the predicate
// lives here, and AssetRegistry.h re-exports it for its own callers.
//=============================================================================

inline constexpr std::string_view kAssetPathPrefix = "asset://";

// True for a non-empty "asset://<something>" path with no backslashes
// (paths are stored with forward slashes only).
[[nodiscard]] bool IsValidAssetPath(std::string_view path);
