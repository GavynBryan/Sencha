#pragma once

// Saving and loading one asset field the way the component serializer does it:
// by the kind the schema declares, with the handle carried as the opaque token
// every handle is. Tests that used to call a per-handle codec go through this,
// so they exercise the path production actually takes.

#include <core/assets/AssetRef.h>
#include <world/serialization/SceneAssetFieldIo.h>

#include <cstdint>
#include <string_view>

struct IReadArchive;
struct IWriteArchive;
struct SceneSerializationContext;

template <typename Handle>
bool SaveHandleField(AssetType type,
                     IWriteArchive& archive,
                     std::string_view key,
                     Handle value,
                     SceneSerializationContext& context)
{
    return SaveAssetField(archive, key, value.ToToken(), type, AssetArity::Single, context);
}

template <typename Handle>
bool LoadHandleField(AssetType type,
                     IReadArchive& archive,
                     std::string_view key,
                     Handle& value,
                     SceneSerializationContext& context)
{
    std::uint64_t token = value.ToToken();
    const bool ok = LoadAssetField(archive, key, token, type, AssetArity::Single, context);
    value = Handle::FromToken(token);
    return ok;
}
