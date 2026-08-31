#include <world/serialization/SceneFieldCodec.h>

#include <core/logging/LoggingProvider.h>

#include <cassert>

// Field types whose scene form is not their in-memory form. Asset references
// are not among them: those are addressed by the kind their schema declares,
// in SceneAssetFieldIo.

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
