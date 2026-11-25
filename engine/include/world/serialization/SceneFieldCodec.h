#pragma once

#include <audio/Caption.h>
#include <core/serialization/Archive.h>
#include <core/text/InlineString.h>
#include <world/serialization/SceneSerializationContext.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

//=============================================================================
// SceneFieldCodec
//
// TypeSchema<T> describes fields and structure. SceneFieldCodec<T> describes
// how each field type is persisted in scene files.
//
// For field types whose scene form differs from their in-memory form: an enum
// that persists as an author-readable string, a fixed-capacity string that
// persists as a plain one. Everything else uses the primary template and its
// archive value.
//
// Asset references are not here. Which asset kind a member refers to is stated
// by its schema (.AsAsset), not by the handle type it is stored in, so they are
// addressed by kind in SceneAssetFieldIo -- one implementation for every kind
// rather than a specialization per handle.
//
// A field type whose Load acquires something also declares Release, the
// inverse: what to let go of when the loaded value is handed on or thrown
// away. Field types that acquire nothing declare nothing; the serializer asks
// whether the operation exists.
//
// WrittenFromValue says the field's scene form depends on its bytes alone, so
// a capture may skip an unchanged row (docs/gameplay/saves.md, "Capture at
// scale"). A specialization that writes through a lookup leaves it out.
//=============================================================================
template<typename T>
struct SceneFieldCodec
{
    static constexpr bool WrittenFromValue = true;

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

namespace SceneEntityReference
{
    // The target's persistent id as text, or empty when it has none; never mints one.
    [[nodiscard]] std::string Text(const EntityId& value, SceneSerializationContext& context);
    // Empty text leaves `value` unset; a target not resident yet waits in Unresolved. False for text that is not an id.
    [[nodiscard]] bool Read(std::string_view text, EntityId& value, SceneSerializationContext& context);
}

// Written as the target's persistent id or as nothing, never minting one; a read
// leaves a target not resident yet in Unresolved (docs/gameplay/saves.md).
template<>
struct SceneFieldCodec<EntityId>
{
    static bool Save(IWriteArchive&, std::string_view, const EntityId&, SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, EntityId&, SceneSerializationContext&);
};

// Every slot in order, each as its target's persistent id or as empty text.
template<std::size_t Count>
struct SceneFieldCodec<std::array<EntityId, Count>>
{
    static bool Save(IWriteArchive& archive, std::string_view key, const std::array<EntityId, Count>& value,
                     SceneSerializationContext& context)
    {
        archive.BeginArray(key, Count);
        for (const EntityId& entity : value)
            archive.Field(std::string_view{}, std::string_view(SceneEntityReference::Text(entity, context)));
        archive.End();
        return archive.Ok();
    }

    static bool Load(IReadArchive& archive, std::string_view key, std::array<EntityId, Count>& value,
                     SceneSerializationContext& context)
    {
        std::size_t count = Count;
        archive.BeginArray(key, count);
        value = {};
        if (archive.IsText() && count > Count)
            archive.MarkInvalidField(key);
        const std::size_t slots = archive.IsText() ? std::min(count, Count) : Count;
        for (std::size_t slot = 0; slot < slots && archive.Ok(); ++slot)
        {
            std::string text;
            archive.Field(std::string_view{}, text);
            if (archive.Ok() && !SceneEntityReference::Read(text, value[slot], context))
                archive.MarkInvalidField(key);
        }
        archive.End();
        return archive.Ok();
    }
};

// Caption enums persist as author-readable strings ("Subtitle", not 1) in
// both text and binary. Unknown strings fail the load — a typo'd kind in
// scene content should surface at load, not silently default.
template<>
struct SceneFieldCodec<CaptionKind>
{
    static constexpr bool WrittenFromValue = true;

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
    static constexpr bool WrittenFromValue = true;

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
    static constexpr bool WrittenFromValue = true;

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
