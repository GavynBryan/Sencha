#pragma once

#include <core/assets/AssetRegistry.h>
#include <render/Material.h>
#include <render/MeshTypes.h>

#include <string_view>

class MaterialCache;
class MeshCache;

class AssetSystem
{
public:
    AssetSystem(AssetRegistry& registry,
                MeshCache& meshes,
                MaterialCache& materials);
    AssetSystem(AssetRegistry& registry,
                MeshCache* meshes,
                MaterialCache* materials);

    [[nodiscard]] MeshHandle LoadMesh(std::string_view path);
    [[nodiscard]] MaterialHandle LoadMaterial(std::string_view path);

    [[nodiscard]] MeshHandle RegisterProceduralMesh(std::string_view path, MeshData mesh);
    [[nodiscard]] MaterialHandle RegisterProceduralMaterial(std::string_view path, Material material);

    [[nodiscard]] std::string_view GetPathForMesh(MeshHandle handle) const;
    [[nodiscard]] std::string_view GetPathForMaterial(MaterialHandle handle) const;

    [[nodiscard]] const AssetRecord* Resolve(std::string_view path, AssetType expectedType) const;

private:
    AssetRegistry& Registry;
    MeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};
