#pragma once

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <string_view>

struct IReadArchive;
struct IWriteArchive;
struct SceneSerializationContext;

//=============================================================================
// SceneAssetFieldIo
//
// Persisting one asset-reference field, addressed by the kind and arity its
// schema declares rather than by the handle type it is stored in. `.AsAsset()`
// is the only place that says which kind a member refers to, so a new asset
// kind is a schema edit and nothing else: there is no second table here mapping
// a handle type back to a kind, and no case to add.
//
// The value travels as its opaque token because that is the one thing every
// handle has in common. Which store the token belongs to is exactly what the
// kind answers.
//
// Reading is strict where the process could hold the asset at all, and quiet
// where it could not: a host composed without a mesh cache declines a mesh
// reference rather than failing the field, because a scene load rolls back
// entirely on an invalid field. See AssetSystem::HasStore.
//=============================================================================

// Writes the reference `token` names. Fails when the process cannot name the
// asset back, which is a reference about to be lost.
[[nodiscard]] bool SaveAssetField(IWriteArchive& archive,
                                  std::string_view key,
                                  std::uint64_t token,
                                  AssetType type,
                                  AssetArity arity,
                                  SceneSerializationContext& context);

// Resolves the persisted reference and leaves an owned token in `token`. The
// caller holds that reference until it hands the value to the component that
// will own it, and then drops it with ReleaseAssetField.
[[nodiscard]] bool LoadAssetField(IReadArchive& archive,
                                  std::string_view key,
                                  std::uint64_t& token,
                                  AssetType type,
                                  AssetArity arity,
                                  SceneSerializationContext& context);

// Drops what LoadAssetField acquired. The component that ends up carrying the
// value takes its own reference through its lifecycle hooks, so the load's
// reference has to go or every entity built from content pins its assets
// forever.
void ReleaseAssetField(std::uint64_t& token,
                       AssetType type,
                       AssetArity arity,
                       SceneSerializationContext& context);
