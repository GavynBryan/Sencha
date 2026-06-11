#pragma once

#include <assets/material/MaterialAssetLoader.h>
#include <assets/static_mesh/StaticMeshAssetLoader.h>
#include <assets/texture/TextureAssetLoader.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/logging/Logger.h>
#include <render/Material.h>
#include <render/TextureHandle.h>
#include <render/static_mesh/StaticMeshData.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <string_view>

class MaterialCache;
class LoggingProvider;
class StaticMeshCache;
class TextureCache;

//=============================================================================
// AssetSystem
//
// The asset front door. Load* resolves a virtual path through the registry
// and composes the type's staged loader (docs/assets/pipeline.md, Decision
// C): dedup check, LoadStaged, CommitTyped — back-to-back on the owner
// thread here; split across the async lane by the manifest-driven zone path
// (Stage 3). One code path, two schedulings.
//=============================================================================
class AssetSystem
{
public:
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache& meshes,
                MaterialCache& materials,
                TextureCache& textures);
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache* meshes,
                MaterialCache* materials,
                TextureCache* textures = nullptr);

    [[nodiscard]] StaticMeshHandle LoadStaticMesh(std::string_view path);
    [[nodiscard]] MaterialHandle LoadMaterial(std::string_view path);

    // `srgb` applies only when the source is loose image bytes (Stage 1 PNG
    // mapping) and only on first load; cooked .stex carries a usage tag that
    // replaces this parameter (docs/assets/pipeline.md, Decisions E/L).
    [[nodiscard]] TextureHandle LoadTexture(std::string_view path, bool srgb = true);

    [[nodiscard]] StaticMeshHandle RegisterProceduralStaticMesh(std::string_view path, StaticMeshData mesh);
    [[nodiscard]] MaterialHandle RegisterProceduralMaterial(std::string_view path, Material material);

    [[nodiscard]] std::string_view GetPathForStaticMesh(StaticMeshHandle handle) const;
    [[nodiscard]] std::string_view GetPathForMaterial(MaterialHandle handle) const;

    [[nodiscard]] const AssetRecord* Resolve(std::string_view path, AssetType expectedType) const;

private:
    Logger& Log;
    AssetRegistry& Registry;
    StaticMeshCache* StaticMeshes = nullptr;
    MaterialCache* Materials = nullptr;
    TextureCache* Textures = nullptr;

    FileAssetSource Source;
    StaticMeshAssetLoader MeshLoader;
    TextureAssetLoader TexLoader;
    MaterialAssetLoader MatLoader;
};
