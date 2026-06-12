#pragma once

#include <audio/AudioClipCache.h>
#include <audio/Caption.h>
#include <core/assets/AssetRef.h>
#include <core/serialization/Archive.h>
#include <core/text/InlineString.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cassert>
#include <cstddef>
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
struct SceneFieldCodec<StaticMeshHandle>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     StaticMeshHandle value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     StaticMeshHandle& value,
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

template<>
struct SceneFieldCodec<AudioClipHandle>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     AudioClipHandle value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     AudioClipHandle& value,
                     SceneSerializationContext& context);
};

// Caption enums persist as author-readable strings ("Subtitle", not 1) in
// both text and binary. Unknown strings fail the load — a typo'd kind in
// scene content should surface at load, not silently default.
template<>
struct SceneFieldCodec<CaptionKind>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     CaptionKind value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     CaptionKind& value,
                     SceneSerializationContext& context);
};

template<>
struct SceneFieldCodec<CaptionPriority>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     CaptionPriority value,
                     SceneSerializationContext& context);

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     CaptionPriority& value,
                     SceneSerializationContext& context);
};

// Inline strings persist as plain string fields in both text and binary.
template<std::size_t Capacity>
struct SceneFieldCodec<InlineString<Capacity>>
{
    static bool Save(IWriteArchive& archive,
                     std::string_view key,
                     const InlineString<Capacity>& value,
                     SceneSerializationContext&)
    {
        archive.Field(key, value.View());
        return archive.Ok();
    }

    static bool Load(IReadArchive& archive,
                     std::string_view key,
                     InlineString<Capacity>& value,
                     SceneSerializationContext&)
    {
        std::string text;
        archive.Field(key, text);
        if (!archive.Ok())
            return false;
        value.Assign(text);
        return true;
    }
};
