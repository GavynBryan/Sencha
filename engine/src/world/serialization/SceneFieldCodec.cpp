#include <world/serialization/SceneFieldCodec.h>

#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>

namespace
{
    Logger& GetSceneLogger(SceneSerializationContext& context)
    {
        assert(context.Logging != nullptr);
        return context.Logging->GetLogger<SceneSerializationContext>();
    }
}

namespace
{
    bool RejectBinaryWrite(IWriteArchive& archive, std::string_view key)
    {
        assert(false && "Binary scene serialization for AssetRef-backed handles is not implemented yet");
        archive.MarkInvalidField(key);
        return false;
    }

    bool RejectBinaryRead(IReadArchive& archive, std::string_view key)
    {
        assert(false && "Binary scene serialization for AssetRef-backed handles is not implemented yet");
        archive.MarkInvalidField(key);
        return false;
    }

    bool ReadLegacyAssetRef(IReadArchive& archive,
                            std::string_view key,
                            AssetType expected,
                            std::string& outPath,
                            SceneSerializationContext& context)
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
            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' has unknown asset type '{}'", key, typeText);
            archive.MarkInvalidField(key);
            return false;
        }

        if (type != expected)
        {
            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' expected asset type '{}', got '{}'",
                                          key, AssetTypeToString(expected), typeText);
            archive.MarkInvalidField(key);
            return false;
        }

        if (path.empty())
        {
            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' has an empty asset path", key);
            archive.MarkMissingField(key);
            return false;
        }

        outPath = std::move(path);
        return true;
    }

    bool ReadTypedAssetPath(IReadArchive& archive,
                            std::string_view key,
                            AssetType expected,
                            std::string& outPath,
                            SceneSerializationContext& context)
    {
        if (archive.IsString(key))
        {
            archive.Field(key, outPath);
            if (!archive.Ok())
                return false;

            if (!outPath.empty())
                return true;

            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' has an empty asset path", key);
            archive.MarkMissingField(key);
            return false;
        }

        if (archive.IsObject(key))
            return ReadLegacyAssetRef(archive, key, expected, outPath, context);

        GetSceneLogger(context).Error(
            "SceneFieldCodec: field '{}' must be an asset path string or legacy AssetRef object", key);
        archive.MarkInvalidField(key);
        return false;
    }

    bool WriteTypedAssetPath(IWriteArchive& archive,
                             std::string_view key,
                             std::string_view path,
                             SceneSerializationContext& context)
    {
        if (path.empty())
        {
            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' has no registered asset path", key);
            archive.MarkInvalidField(key);
            return false;
        }

        archive.Field(key, path);
        return archive.Ok();
    }
}

bool SceneFieldCodec<StaticMeshHandle>::Save(IWriteArchive& archive,
                                             std::string_view key,
                                             StaticMeshHandle value,
                                             SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryWrite(archive, key);

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<StaticMeshHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    return WriteTypedAssetPath(archive, key, context.Assets->GetPathForStaticMesh(value), context);
}

bool SceneFieldCodec<StaticMeshHandle>::Load(IReadArchive& archive,
                                             std::string_view key,
                                             StaticMeshHandle& value,
                                             SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    std::string path;
    if (!ReadTypedAssetPath(archive, key, AssetType::StaticMesh, path, context))
        return false;

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<StaticMeshHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    value = context.Assets->LoadStaticMesh(path);
    if (!value.IsValid())
    {
        GetSceneLogger(context).Error("SceneFieldCodec<StaticMeshHandle>: failed to load static mesh asset '{}'",
                                      path);
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

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<MaterialHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    return WriteTypedAssetPath(archive, key, context.Assets->GetPathForMaterial(value), context);
}

bool SceneFieldCodec<MaterialHandle>::Load(IReadArchive& archive,
                                           std::string_view key,
                                           MaterialHandle& value,
                                           SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    std::string path;
    if (!ReadTypedAssetPath(archive, key, AssetType::Material, path, context))
        return false;

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<MaterialHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    value = context.Assets->LoadMaterial(path);
    if (!value.IsValid())
    {
        GetSceneLogger(context).Error("SceneFieldCodec<MaterialHandle>: failed to load material asset '{}'", path);
        archive.MarkInvalidField(key);
        return false;
    }

    return archive.Ok();
}
