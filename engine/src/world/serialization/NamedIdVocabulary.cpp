#include <world/serialization/NamedIdVocabulary.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/Archive.h>
#include <world/serialization/SceneAssetRef.h>
#include <world/serialization/SceneSerializationContext.h>

#include <string>

bool SaveNamedId(IWriteArchive& archive, std::string_view key, const NamedIdVocabulary& vocabulary, std::uint32_t id,
                 const World* world, SceneSerializationContext& context)
{
    if (id != 0 && world == nullptr)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    const std::string_view name = id != 0 ? vocabulary.NameOf(*world, id) : std::string_view{};
    if (!archive.IsText())
        archive.Field(key, name);
    else if (name.empty())
        return archive.Ok();
    else if (vocabulary.Kind == NamedIdKind::DataDefinition)
        return WriteSceneAssetRef(archive, key, AssetType::Data, name, context);
    else
        archive.Field(key, name);
    return archive.Ok();
}

bool LoadNamedId(IReadArchive& archive, std::string_view key, const NamedIdVocabulary& vocabulary, std::uint32_t& id,
                 World* world, SceneSerializationContext& context)
{
    std::string name;
    if (!archive.IsText())
        archive.Field(key, name);
    else if (!archive.HasField(key))
        name.clear();
    else if (vocabulary.Kind == NamedIdKind::DataDefinition)
    {
        if (!ReadSceneAssetRef(archive, key, AssetType::Data, name, context))
            return false;
    }
    else
        archive.Field(key, name);
    if (!archive.Ok())
        return false;
    if (world == nullptr)
    {
        id = 0;
        if (name.empty())
            return true;
        archive.MarkInvalidField(key);
        return false;
    }

    id = vocabulary.Resolve(*world, name);
    if (name.empty())
        return true;
    auto& log = context.Logging->GetLogger<SceneSerializationContext>();
    if (id == 0)
    {
        log.Warn("'{}' names {} '{}', which this content does not declare", key, vocabulary.Label, name);
        archive.MarkInvalidField(key);
        return false;
    }
    if (const std::string_view resolved = vocabulary.NameOf(*world, id); resolved != name)
        log.Warn("'{}' names {} '{}', which is not registered; it reads as '{}'", key, vocabulary.Label, name, resolved);
    return true;
}
