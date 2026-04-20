#include <world/serialization/SceneFieldCodec.h>

#include <core/assets/AssetSystem.h>
#include <render/MaterialCache.h>
#include <render/MeshCache.h>

namespace
{
    bool RejectBinaryWrite(IWriteArchive& archive, std::string_view key)
    {
        assert(false && "Binary scene serialization for AssetRef-backed handles is not implemented yet");
        std::fprintf(stderr,
            "SceneFieldCodec: binary scene serialization for field \"%.*s\" is not implemented yet\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    bool RejectBinaryRead(IReadArchive& archive, std::string_view key)
    {
        assert(false && "Binary scene serialization for AssetRef-backed handles is not implemented yet");
        std::fprintf(stderr,
            "SceneFieldCodec: binary scene deserialization for field \"%.*s\" is not implemented yet\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    bool ReadLegacyAssetRef(IReadArchive& archive,
                            std::string_view key,
                            AssetType expected,
                            std::string& outPath)
    {
        std::string typeText;
        std::string path;

        archive.BeginObject(key);
        archive.Field(std::string_view{"type"}, typeText);
        archive.Field(std::string_view{"path"}, path);
        archive.End();

        if (!archive.Ok())
            return false;

        AssetType type = AssetType::Unknown;
        if (!AssetTypeFromString(typeText, type))
        {
            std::fprintf(stderr,
                "SceneFieldCodec: field \"%.*s\" has unknown asset type \"%s\"\n",
                static_cast<int>(key.size()), key.data(), typeText.c_str());
            archive.MarkInvalidField(key);
            return false;
        }

        if (type != expected)
        {
            std::fprintf(stderr,
                "SceneFieldCodec: field \"%.*s\" expected asset type \"%.*s\", got \"%s\"\n",
                static_cast<int>(key.size()), key.data(),
                static_cast<int>(AssetTypeToString(expected).size()), AssetTypeToString(expected).data(),
                typeText.c_str());
            archive.MarkInvalidField(key);
            return false;
        }

        if (path.empty())
        {
            std::fprintf(stderr,
                "SceneFieldCodec: field \"%.*s\" has an empty asset path\n",
                static_cast<int>(key.size()), key.data());
            archive.MarkMissingField(key);
            return false;
        }

        outPath = std::move(path);
        return true;
    }

    bool ReadTypedAssetPath(IReadArchive& archive,
                            std::string_view key,
                            AssetType expected,
                            std::string& outPath)
    {
        if (archive.IsString(key))
        {
            archive.Field(key, outPath);
            if (!archive.Ok())
                return false;

            if (!outPath.empty())
                return true;

            std::fprintf(stderr,
                "SceneFieldCodec: field \"%.*s\" has an empty asset path\n",
                static_cast<int>(key.size()), key.data());
            archive.MarkMissingField(key);
            return false;
        }

        if (archive.IsObject(key))
            return ReadLegacyAssetRef(archive, key, expected, outPath);

        std::fprintf(stderr,
            "SceneFieldCodec: field \"%.*s\" must be an asset path string or legacy AssetRef object\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    bool WriteTypedAssetPath(IWriteArchive& archive,
                             std::string_view key,
                             std::string_view path)
    {
        if (path.empty())
        {
            std::fprintf(stderr,
                "SceneFieldCodec: field \"%.*s\" has no registered asset path\n",
                static_cast<int>(key.size()), key.data());
            archive.MarkInvalidField(key);
            return false;
        }

        archive.Field(key, path);
        return archive.Ok();
    }
}

bool SceneFieldCodec<MeshHandle>::Save(IWriteArchive& archive,
                                       std::string_view key,
                                       MeshHandle value,
                                       SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryWrite(archive, key);

    if (!context.Meshes)
    {
        std::fprintf(stderr, "SceneFieldCodec<MeshHandle>: missing MeshCache for field \"%.*s\"\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    return WriteTypedAssetPath(archive, key, context.Meshes->GetName(value));
}

bool SceneFieldCodec<MeshHandle>::Load(IReadArchive& archive,
                                       std::string_view key,
                                       MeshHandle& value,
                                       SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    std::string path;
    if (!ReadTypedAssetPath(archive, key, AssetType::Mesh, path))
        return false;

    if (!context.Assets)
    {
        std::fprintf(stderr, "SceneFieldCodec<MeshHandle>: missing AssetSystem for field \"%.*s\"\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    value = context.Assets->LoadMesh(path);
    if (!value.IsValid())
    {
        std::fprintf(stderr,
            "SceneFieldCodec<MeshHandle>: failed to load mesh asset \"%s\"\n",
            path.c_str());
        archive.MarkInvalidField(key);
        return false;
    }

    return archive.Ok();
}

bool SceneFieldCodec<MaterialHandle>::Save(IWriteArchive& archive,
                                           std::string_view key,
                                           MaterialHandle value,
                                           SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryWrite(archive, key);

    if (!context.Materials)
    {
        std::fprintf(stderr, "SceneFieldCodec<MaterialHandle>: missing MaterialCache for field \"%.*s\"\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    return WriteTypedAssetPath(archive, key, context.Materials->GetName(value));
}

bool SceneFieldCodec<MaterialHandle>::Load(IReadArchive& archive,
                                           std::string_view key,
                                           MaterialHandle& value,
                                           SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    std::string path;
    if (!ReadTypedAssetPath(archive, key, AssetType::Material, path))
        return false;

    if (!context.Assets)
    {
        std::fprintf(stderr, "SceneFieldCodec<MaterialHandle>: missing AssetSystem for field \"%.*s\"\n",
            static_cast<int>(key.size()), key.data());
        archive.MarkInvalidField(key);
        return false;
    }

    value = context.Assets->LoadMaterial(path);
    if (!value.IsValid())
    {
        std::fprintf(stderr,
            "SceneFieldCodec<MaterialHandle>: failed to load material asset \"%s\"\n",
            path.c_str());
        archive.MarkInvalidField(key);
        return false;
    }

    return archive.Ok();
}
