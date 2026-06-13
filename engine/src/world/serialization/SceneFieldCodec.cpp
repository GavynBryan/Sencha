#include <world/serialization/SceneFieldCodec.h>

#include <core/assets/AssetId.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>

#include <optional>

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

    // Reads the legacy {"type": ..., "path": ...} ref object. The enclosing
    // object scope is already open.
    bool ReadLegacyAssetRefFields(IReadArchive& archive,
                                  std::string_view key,
                                  AssetType expected,
                                  std::string& outPath,
                                  SceneSerializationContext& context)
    {
        std::string typeText;
        std::string path;

        archive.Field(std::string_view{"type"}, typeText);
        archive.Field(std::string_view{"path"}, path);

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

    // Reads the cooked-scene {"id": "<hex>", "path": ...} ref object
    // (docs/assets/pipeline.md, Decision A / Stage 4e): the id wins when
    // the registry knows it — that is what survives a rename the stamped
    // path predates — and the stamped path is the fallback otherwise. The
    // enclosing object scope is already open. Authored scenes keep writing
    // bare path strings; the editor round trip never produces this form.
    bool ReadStampedAssetRefFields(IReadArchive& archive,
                                   std::string_view key,
                                   AssetType expected,
                                   std::string& outPath,
                                   SceneSerializationContext& context)
    {
        std::string idText;
        archive.Field(std::string_view{"id"}, idText);

        std::string path;
        if (archive.HasField(std::string_view{"path"}))
            archive.Field(std::string_view{"path"}, path);

        if (!archive.Ok())
            return false;

        const std::optional<AssetId> id = AssetIdFromString(idText);
        if (!id.has_value())
        {
            GetSceneLogger(context).Error("SceneFieldCodec: field '{}' has malformed asset id '{}'",
                                          key, idText);
            archive.MarkInvalidField(key);
            return false;
        }

        outPath = context.Assets
            ? std::string(context.Assets->ResolveRefPath(*id, path, expected))
            : std::move(path);

        if (outPath.empty())
        {
            GetSceneLogger(context).Error(
                "SceneFieldCodec: field '{}' has unknown asset id {} and no fallback path",
                key, AssetIdToString(*id));
            archive.MarkInvalidField(key);
            return false;
        }

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
        {
            archive.BeginObject(key);
            const bool stamped = archive.HasField(std::string_view{"id"});
            const bool ok = stamped
                ? ReadStampedAssetRefFields(archive, key, expected, outPath, context)
                : ReadLegacyAssetRefFields(archive, key, expected, outPath, context);
            archive.End();
            return ok && archive.Ok();
        }

        GetSceneLogger(context).Error(
            "SceneFieldCodec: field '{}' must be an asset path string or an AssetRef object", key);
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

bool SceneFieldCodec<AudioClipHandle>::Save(IWriteArchive& archive,
                                            std::string_view key,
                                            AudioClipHandle value,
                                            SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryWrite(archive, key);

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<AudioClipHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    return WriteTypedAssetPath(archive, key, context.Assets->GetPathForAudioClip(value), context);
}

bool SceneFieldCodec<AudioClipHandle>::Load(IReadArchive& archive,
                                            std::string_view key,
                                            AudioClipHandle& value,
                                            SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    std::string path;
    if (!ReadTypedAssetPath(archive, key, AssetType::Audio, path, context))
        return false;

    if (!context.Assets)
    {
        GetSceneLogger(context).Error("SceneFieldCodec<AudioClipHandle>: missing AssetSystem for field '{}'", key);
        archive.MarkInvalidField(key);
        return false;
    }

    value = context.Assets->LoadAudioClip(path);
    if (!value.IsValid())
    {
        GetSceneLogger(context).Error("SceneFieldCodec<AudioClipHandle>: failed to load audio clip asset '{}'", path);
        archive.MarkInvalidField(key);
        return false;
    }

    return archive.Ok();
}

namespace
{
    // Shared load body for the caption enum codecs: persisted as strings in
    // both text and binary, an unknown string fails the load so content
    // typos surface at load time.
    template <typename Enum, typename FromString>
    bool LoadEnumField(IReadArchive& archive, std::string_view key, Enum& value,
                       SceneSerializationContext& context, FromString fromString,
                       std::string_view enumName)
    {
        std::string text;
        archive.Field(key, text);
        if (!archive.Ok())
            return false;

        const auto parsed = fromString(text);
        if (!parsed)
        {
            GetSceneLogger(context).Error(
                "SceneFieldCodec<{}>: unknown value '{}' for field '{}'",
                enumName, text, key);
            archive.MarkInvalidField(key);
            return false;
        }

        value = *parsed;
        return true;
    }
}

bool SceneFieldCodec<CaptionKind>::Save(IWriteArchive& archive,
                                        std::string_view key,
                                        CaptionKind value,
                                        SceneSerializationContext&)
{
    archive.Field(key, CaptionKindToString(value));
    return archive.Ok();
}

bool SceneFieldCodec<CaptionKind>::Load(IReadArchive& archive,
                                        std::string_view key,
                                        CaptionKind& value,
                                        SceneSerializationContext& context)
{
    return LoadEnumField(archive, key, value, context,
                         CaptionKindFromString, "CaptionKind");
}

bool SceneFieldCodec<CaptionPriority>::Save(IWriteArchive& archive,
                                            std::string_view key,
                                            CaptionPriority value,
                                            SceneSerializationContext&)
{
    archive.Field(key, CaptionPriorityToString(value));
    return archive.Ok();
}

bool SceneFieldCodec<CaptionPriority>::Load(IReadArchive& archive,
                                            std::string_view key,
                                            CaptionPriority& value,
                                            SceneSerializationContext& context)
{
    return LoadEnumField(archive, key, value, context,
                         CaptionPriorityFromString, "CaptionPriority");
}
