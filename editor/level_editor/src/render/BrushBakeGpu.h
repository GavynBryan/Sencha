#pragma once

#include "BrushBakeCache.h"

#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RuntimeAssets.h>
#include <render/static_mesh/StaticMeshCache.h>

// The bake cache's only GPU touches, bound to the editor's shared asset
// caches: static meshes are created and destroyed in the mesh cache, materials
// are leased and released through the asset system. Every builder owner (the
// render feature, the thumbnail cache) binds the same way.
[[nodiscard]] inline BrushBakeCache::Gpu MakeBrushBakeGpu(RuntimeAssets& runtimeAssets)
{
    RuntimeAssets* assets = &runtimeAssets;
    return BrushBakeCache::Gpu{
        .Bake = [assets](const MeshGeometry& geometry)
        { return assets->StaticMeshes->Create(geometry); },
        .Destroy = [assets](StaticMeshHandle handle)
        { assets->StaticMeshes->Destroy(handle); },
        .Lease = [assets](const AssetRef& ref)
        {
            return MaterialHandle::FromToken(
                assets->Assets.LoadLease(ref.Path, AssetType::Material).Relinquish());
        },
        .Release = [assets](MaterialHandle material)
        { assets->Assets.ReleaseLease(AssetType::Material, material.ToToken()); },
    };
}
