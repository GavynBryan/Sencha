#include <world/serialization/SceneAssetFieldIo.h>

#include <world/serialization/SceneAssetRef.h>

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetId.h>
#include <core/assets/AssetLease.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/Archive.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
    Logger& GetSceneLogger(SceneSerializationContext& context)
    {
        assert(context.Logging != nullptr);
        return context.Logging->GetLogger<SceneSerializationContext>();
    }

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

    // Whether this process could hold `type` at all, as opposed to whether a
    // particular asset resolved.
    //
    // A scene names the content it is made of, not the processes that read it.
    // A dedicated host has no cache that can hold a mesh, so a mesh reference
    // is something it declines rather than something it fails on -- and it must
    // decline without failing the field, because a scene load rolls back
    // entirely on an invalid field and a host would then have no collision to
    // simulate against. The handle is left invalid, which every consumer of an
    // asset handle already tolerates.
    //
    // Only a capability this process was composed without takes this path.
    // Where the capability exists, an asset that will not resolve is still a
    // hard failure, which is what keeps a client or an editor strict about the
    // content it is supposed to be able to load.
    bool LacksCapability(AssetType type, SceneSerializationContext& context)
    {
        return context.Assets != nullptr && !context.Assets->HasStore(type);
    }

    bool MissingAssetSystem(IWriteArchive& archive,
                            std::string_view key,
                            AssetType type,
                            SceneSerializationContext& context)
    {
        GetSceneLogger(context).Error("SceneAssetField: missing AssetSystem for {} field '{}'",
                                      AssetTypeToString(type), key);
        archive.MarkInvalidField(key);
        return false;
    }

    bool MissingAssetSystem(IReadArchive& archive,
                            std::string_view key,
                            AssetType type,
                            SceneSerializationContext& context)
    {
        GetSceneLogger(context).Error("SceneAssetField: missing AssetSystem for {} field '{}'",
                                      AssetTypeToString(type), key);
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

    // ── The list arity ───────────────────────────────────────────────────────
    //
    // A list is not a token in a store like every other reference: it persists
    // as an array of refs and is interned into the kind's list store, whose
    // token the component carries. That is a different representation, not a
    // different asset kind, so it is one branch on arity here.

    bool SaveAssetList(IWriteArchive& archive,
                       std::string_view key,
                       std::uint64_t token,
                       AssetType type,
                       SceneSerializationContext& context)
    {
        const std::vector<std::uint64_t> members = context.Assets->ListMembers(type, token);
        archive.BeginArray(key, members.size());
        for (const std::uint64_t member : members)
        {
            // Key is ignored inside an array scope; the element is appended.
            if (!WriteSceneAssetRef(archive, key, context.Assets->GetPathForLease(type, member), context))
            {
                archive.End();
                return false;
            }
        }
        archive.End();
        return archive.Ok();
    }

    bool LoadAssetList(IReadArchive& archive,
                       std::string_view key,
                       std::uint64_t& token,
                       AssetType type,
                       SceneSerializationContext& context)
    {
        // Each member's load reference is held only until the list takes its
        // own, so a list that fails to intern leaves nothing behind.
        std::vector<AssetLease> members;
        const auto resolveInto = [&](std::string_view refKey) {
            std::string path;
            if (!ReadSceneAssetRef(archive, refKey, type, path, context))
                return false;
            AssetLease member = context.Assets->LoadLease(path, type);
            if (!member.IsValid())
            {
                GetSceneLogger(context).Error("SceneAssetField: failed to load {} asset '{}'",
                                              AssetTypeToString(type), path);
                archive.MarkInvalidField(refKey);
                return false;
            }
            members.push_back(std::move(member));
            return true;
        };

        if (archive.HasField(key))
        {
            std::size_t count = 0;
            archive.BeginArray(key, count);
            if (!archive.Ok())
            {
                archive.End();
                return false;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!resolveInto(key))
                {
                    archive.End();
                    return false;
                }
            }
            archive.End();
        }
        else if (type == AssetType::Material && archive.HasField(std::string_view{"material"}))
        {
            // Legacy single-material scene form, before per-section binding.
            if (!resolveInto(std::string_view{"material"}))
                return false;
        }
        else
        {
            GetSceneLogger(context).Error("SceneAssetField: field '{}' is missing", key);
            archive.MarkMissingField(key);
            return false;
        }

        std::vector<std::uint64_t> tokens;
        tokens.reserve(members.size());
        for (const AssetLease& member : members)
            tokens.push_back(member.OpaqueToken());

        AssetLease list = context.Assets->InternList(type, tokens);
        if (!list.IsValid())
        {
            GetSceneLogger(context).Error("SceneAssetField: could not intern {} list for '{}'",
                                          AssetTypeToString(type), key);
            archive.MarkInvalidField(key);
            return false;
        }
        token = list.Relinquish();
        return archive.Ok();
    }
}

bool ReadSceneAssetRef(IReadArchive& archive,
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

        GetSceneLogger(context).Error("SceneAssetRef: field '{}' has an empty asset path", key);
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
        "SceneAssetRef: field '{}' must be an asset path string or an AssetRef object", key);
    archive.MarkInvalidField(key);
    return false;
}

bool WriteSceneAssetRef(IWriteArchive& archive,
                        std::string_view key,
                        std::string_view path,
                        SceneSerializationContext& context)
{
    if (path.empty())
    {
        GetSceneLogger(context).Error("SceneAssetRef: field '{}' has no registered asset path", key);
        archive.MarkInvalidField(key);
        return false;
    }

    archive.Field(key, path);
    return archive.Ok();
}

bool SaveAssetField(IWriteArchive& archive,
                    std::string_view key,
                    std::uint64_t token,
                    AssetType type,
                    AssetArity arity,
                    SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryWrite(archive, key);

    if (!context.Assets)
        return MissingAssetSystem(archive, key, type, context);

    if (arity == AssetArity::List)
        return SaveAssetList(archive, key, token, type, context);

    return WriteSceneAssetRef(archive, key, context.Assets->GetPathForLease(type, token), context);
}

bool LoadAssetField(IReadArchive& archive,
                    std::string_view key,
                    std::uint64_t& token,
                    AssetType type,
                    AssetArity arity,
                    SceneSerializationContext& context)
{
    if (!archive.IsText())
        return RejectBinaryRead(archive, key);

    if (arity == AssetArity::List)
    {
        if (!context.Assets)
            return MissingAssetSystem(archive, key, type, context);
        if (LacksCapability(type, context))
            return archive.Ok();
        return LoadAssetList(archive, key, token, type, context);
    }

    std::string path;
    if (!ReadSceneAssetRef(archive, key, type, path, context))
        return false;

    if (!context.Assets)
        return MissingAssetSystem(archive, key, type, context);

    if (LacksCapability(type, context))
        return archive.Ok();

    // The lease's reference becomes the caller's, exactly as the typed loads it
    // replaces did: whoever holds the token owns one reference until they
    // release it.
    AssetLease lease = context.Assets->LoadLease(path, type);
    if (!lease.IsValid())
    {
        GetSceneLogger(context).Error("SceneAssetField: failed to load {} asset '{}'",
                                      AssetTypeToString(type), path);
        archive.MarkInvalidField(key);
        return false;
    }

    token = lease.Relinquish();
    return archive.Ok();
}

void ReleaseAssetField(std::uint64_t& token,
                       AssetType type,
                       AssetArity arity,
                       SceneSerializationContext& context)
{
    if (context.Assets == nullptr)
        return;

    context.Assets->ReleaseLease(type, token, arity);
}
