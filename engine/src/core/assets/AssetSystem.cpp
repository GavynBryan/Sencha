#include <core/assets/AssetSystem.h>

#include <render/MaterialCache.h>
#include <render/MeshCache.h>

#include <cstdio>

AssetSystem::AssetSystem(AssetRegistry& registry,
                         MeshCache& meshes,
                         MaterialCache& materials)
    : Registry(registry)
    , Meshes(&meshes)
    , Materials(&materials)
{
}

AssetSystem::AssetSystem(AssetRegistry& registry,
                         MeshCache* meshes,
                         MaterialCache* materials)
    : Registry(registry)
    , Meshes(meshes)
    , Materials(materials)
{
}

const AssetRecord* AssetSystem::Resolve(std::string_view path, AssetType expectedType) const
{
    if (path.empty())
    {
        std::fprintf(stderr, "AssetSystem: empty asset path\n");
        return nullptr;
    }

    const AssetRecord* record = Registry.FindByPath(path);
    if (!record)
    {
        std::fprintf(stderr, "AssetSystem: asset path not registered: %.*s\n",
            static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    if (record->Type != expectedType)
    {
        std::fprintf(stderr, "AssetSystem: expected %.*s asset, got %.*s for path %s\n",
            static_cast<int>(AssetTypeToString(expectedType).size()),
            AssetTypeToString(expectedType).data(),
            static_cast<int>(AssetTypeToString(record->Type).size()),
            AssetTypeToString(record->Type).data(),
            record->Path.c_str());
        return nullptr;
    }

    return record;
}

MeshHandle AssetSystem::LoadMesh(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Mesh);
    if (!record)
        return {};

    if (!record->FilePath.empty())
    {
        std::fprintf(stderr, "AssetSystem: mesh asset loading from file not implemented: %s\n",
            record->FilePath.c_str());
        return {};
    }

    if (!Meshes)
    {
        std::fprintf(stderr, "AssetSystem: missing MeshCache for mesh asset %s\n",
            record->Path.c_str());
        return {};
    }

    MeshHandle handle = Meshes->Acquire(record->Path);
    if (!handle.IsValid())
    {
        std::fprintf(stderr, "AssetSystem: mesh cache has no runtime resource for path %s\n",
            record->Path.c_str());
        return {};
    }

    return handle;
}

MaterialHandle AssetSystem::LoadMaterial(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Material);
    if (!record)
        return {};

    if (!record->FilePath.empty())
    {
        std::fprintf(stderr, "AssetSystem: material asset loading from file not implemented: %s\n",
            record->FilePath.c_str());
        return {};
    }

    if (!Materials)
    {
        std::fprintf(stderr, "AssetSystem: missing MaterialCache for material asset %s\n",
            record->Path.c_str());
        return {};
    }

    MaterialHandle handle = Materials->Acquire(record->Path);
    if (!handle.IsValid())
    {
        std::fprintf(stderr, "AssetSystem: material cache has no runtime resource for path %s\n",
            record->Path.c_str());
        return {};
    }

    return handle;
}
