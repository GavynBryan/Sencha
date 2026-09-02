#pragma once

#include <assets/static_mesh/MeshLoader.h>
#include <core/assets/AssetStager.h>
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
// SkinnedMeshCache, resolving the skeleton ref through the front door it is
// handed and holding it (the mesh→skeleton refcount chain). Payload type:
// SkinnedMeshData.
//=============================================================================
class SkinnedMeshAssetLoader final : public IAssetStager
{
public:
    SkinnedMeshAssetLoader(LoggingProvider& logging,
                           SkinnedMeshCache* cache,
                           SkeletonCache* skeletons);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    [[nodiscard]] SkinnedMeshHandle CommitTyped(AssetStaging&& staged, AssetSystem& assets);

private:
    Logger& Log;
    SkinnedMeshCache* Cache = nullptr;
    SkeletonCache* Skeletons = nullptr;
    MeshLoader FileLoader;
};
