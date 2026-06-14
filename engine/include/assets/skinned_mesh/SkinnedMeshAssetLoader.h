#pragma once

#include <assets/static_mesh/MeshLoader.h>
#include <core/assets/AssetLoader.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>

class AssetSystem;
class LoggingProvider;
class SkeletonCache;
class SkinnedMeshCache;

//=============================================================================
// SkinnedMeshAssetLoader (docs/assets/pipeline.md, Decisions C, J, M, N)
//
// Staged-load contract for .skmesh. Stage: bytes -> SkinnedMeshData (the
// skinned path rejects a static file). Commit: upload the rest geometry via
// SkinnedMeshCache, resolving the skeleton ref through the front door and
// holding it (the mesh→skeleton refcount chain). Payload type:
// SkinnedMeshData.
//=============================================================================
class SkinnedMeshAssetLoader final : public IAssetLoader
{
public:
    SkinnedMeshAssetLoader(LoggingProvider& logging,
                           AssetSystem& assets,
                           SkinnedMeshCache* cache,
                           SkeletonCache* skeletons);

    [[nodiscard]] AssetType Type() const override { return AssetType::SkinnedMesh; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    [[nodiscard]] SkinnedMeshHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    AssetSystem& Assets;
    SkinnedMeshCache* Cache = nullptr;
    SkeletonCache* Skeletons = nullptr;
    MeshLoader FileLoader;
};
