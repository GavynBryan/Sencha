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

    [[nodiscard]] const AssetRecord* Resolve(std::string_view path, AssetType expectedType) const;

private:
    AssetRegistry& Registry;
    MeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};
