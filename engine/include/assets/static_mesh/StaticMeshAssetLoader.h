#pragma once

#include <assets/static_mesh/MeshLoader.h>
#include <core/assets/AssetLoader.h>
#include <render/static_mesh/StaticMeshHandle.h>

class LoggingProvider;
class StaticMeshCache;

//=============================================================================
// StaticMeshAssetLoader
//
// Staged-load contract for .smesh (docs/assets/pipeline.md, Decision C).
// Stage: bytes -> MeshGeometry (the static path rejects a skinned file).
// Commit: GPU upload via StaticMeshCache. Payload type: MeshGeometry.
// Skinned meshes are a distinct asset type (SkinnedMeshAssetLoader).
//=============================================================================
class StaticMeshAssetLoader final : public IAssetLoader
{
public:
    StaticMeshAssetLoader(LoggingProvider& logging, StaticMeshCache* cache);

    [[nodiscard]] AssetType Type() const override { return AssetType::StaticMesh; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller). The virtual Commit wraps this for heterogeneous drivers.
    [[nodiscard]] StaticMeshHandle CommitTyped(AssetStaging&& staged);

    // Owner-thread hot-reload commit (Stage 6b): swaps the staged geometry into
    // the existing resident entry in place (same handle, no new slot), retiring
    // the old GPU buffers. Returns false if the mesh is not resident — there is
    // nothing to reload, and the refreshed registry hash means the next normal
    // load picks up the new bytes.
    [[nodiscard]] bool CommitReload(AssetStaging&& staged);

private:
    Logger& Log;
    StaticMeshCache* Cache = nullptr;
    MeshLoader FileLoader;
};
