#pragma once

#include <assets/static_mesh/StaticMeshLoader.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <render/Material.h>
#include <render/TextureHandle.h>
#include <render/static_mesh/StaticMeshData.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <string_view>
#include <vector>

class MaterialCache;
class LoggingProvider;
class StaticMeshCache;
class TextureCache;

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
    // Resolves one material texture slot: loads the texture, writes its
    // bindless index into `outIndex`, and appends the owning reference. An
    // empty ref or a failed load leaves `outIndex` at the slot's neutral
    // default (UINT32_MAX).
    void ResolveMaterialTextureSlot(const AssetRef& ref,
                                    bool srgb,
                                    uint32_t& outIndex,
                                    std::vector<TextureCacheHandle>& owned);

    Logger& Log;
    AssetRegistry& Registry;
    StaticMeshCache* StaticMeshes = nullptr;
    MaterialCache* Materials = nullptr;
    TextureCache* Textures = nullptr;
    StaticMeshLoader StaticMeshFileLoader;
};
