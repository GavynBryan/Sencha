#pragma once

#include <core/assets/AssetRef.h>
#include <core/serialization/Archive.h>
#include <render/Material.h>
#include <render/MeshTypes.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

//=============================================================================
// SceneFieldCodec
//
// TypeSchema<T> describes fields and structure. SceneFieldCodec<T> describes
// how each field type is persisted in scene files.
//=============================================================================
template<typename T>
struct SceneFieldCodec
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     const T& value,
                     SceneSerializationContext&)
    {
        WriteArchiveValue(archive, key, value);
        return archive.Ok();
    }

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     T& value,
                     SceneSerializationContext&)
    {
        ReadArchiveValue(archive, key, value);
        return archive.Ok();
    }
};

template<>
struct SceneFieldCodec<MeshHandle>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     MeshHandle value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     MeshHandle& value,
                     SceneSerializationContext& context);
};

template<>
struct SceneFieldCodec<MaterialHandle>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     MaterialHandle value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     MaterialHandle& value,
                     SceneSerializationContext& context);
};

