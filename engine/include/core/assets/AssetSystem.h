#pragma once

#include <assets/static_mesh/StaticMeshLoader.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshData.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <string_view>

class MaterialCache;
class LoggingProvider;
class StaticMeshCache;

class AssetSystem
{
public:
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache& meshes,
                MaterialCache& materials);
    AssetSystem(LoggingProvider& logging,
                AssetRegistry& registry,
                StaticMeshCache* meshes,
                MaterialCache* materials);

    [[nodiscard]] StaticMeshHandle LoadStaticMesh(std::string_view path);
    [[nodiscard]] MaterialHandle LoadMaterial(std::string_view path);

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
    StaticMeshLoader StaticMeshFileLoader;
};
