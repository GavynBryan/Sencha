#include <gameplay_tags/GameplayTagSerialization.h>

#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <core/serialization/Archive.h>

#include <cstdint>
#include <string>
#include <string_view>

// Format: { "tags": [ { "name": "<dot.path>", "stacks": <uint> }, ... ] }.
//
// Binary note: like asset-backed handle codecs, this is JSON-first. Binary read
// archives do not store array element counts (BeginArray yields 0), so the tag
// list does not round-trip through the binary path; that mirrors the existing
// handle-codec limitation and is acceptable until binary scene support matures.

bool WriteGameplayTags(IWriteArchive& archive,
                       const GameplayTagContainer& tags,
                       const GameplayTagRegistry& registry)
{
    std::size_t persisted = 0;
    for (int i = 0; i < tags.Count; ++i)
        persisted += tags.Baseline[i] > 0 ? 1 : 0;
    archive.BeginObject(std::string_view{});
    archive.BeginArray(std::string_view{ "tags" }, persisted);
    for (int i = 0; i < tags.Count; ++i)
    {
        if (tags.Baseline[i] == 0)
            continue;
        archive.BeginObject(std::string_view{});
        archive.Field(std::string_view{ "name" }, registry.GetName(tags.Tags[i]));
        archive.Field(std::string_view{ "stacks" }, static_cast<std::uint32_t>(tags.Baseline[i]));
        archive.End();
    }
    archive.End(); // tags array
    archive.End(); // root object
    return archive.Ok();
}

bool ReadGameplayTags(IReadArchive& archive,
                      GameplayTagContainer& tags,
                      const GameplayTagRegistry& registry,
                      std::vector<std::string>* dropped)
{
    tags.Clear();

    archive.BeginObject(std::string_view{});
    std::size_t count = 0;
    archive.BeginArray(std::string_view{ "tags" }, count);
    for (std::size_t i = 0; i < count; ++i)
    {
        archive.BeginObject(std::string_view{});
        std::string name;
        std::uint32_t stacks = 0;
        archive.Field(std::string_view{ "name" }, name);
        archive.Field(std::string_view{ "stacks" }, stacks);
        archive.End();
        if (!archive.Ok())
            break;

        const GameplayTagId id = registry.FindTag(name);
        if (!id.IsValid())
            continue; // tag not in this registry's vocabulary: skip, keep loading
        if (!registry.ProjectedBy(id).empty())
        {
            if (dropped != nullptr)
                dropped->push_back(std::move(name));
            continue;
        }

        tags.GrantBaseline(id, static_cast<std::uint16_t>(stacks == 0 ? 1u : stacks));
    }
    archive.End(); // tags array
    archive.End(); // root object
    return archive.Ok();
}
