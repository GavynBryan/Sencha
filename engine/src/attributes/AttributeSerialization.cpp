#include <attributes/AttributeSerialization.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>

#include <core/serialization/Archive.h>

#include <string>
#include <string_view>

// Format: { "attributes": [ { "name": "<name>", "base": <float> }, ... ] }

bool WriteAttributes(IWriteArchive& archive,
                     const AttributeSet& attributes,
                     const AttributeRegistry& registry)
{
    archive.BeginObject(std::string_view{});
    archive.BeginArray(std::string_view{ "attributes" }, attributes.Count);
    for (int i = 0; i < attributes.Count; ++i)
    {
        archive.BeginObject(std::string_view{});
        archive.Field(std::string_view{ "name" }, registry.GetName(attributes.Ids[i]));
        archive.Field(std::string_view{ "base" }, attributes.Base[i]);
        archive.End();
    }
    archive.End(); // attributes array
    archive.End(); // root object
    return archive.Ok();
}

bool ReadAttributes(IReadArchive& archive,
                    AttributeSet& attributes,
                    const AttributeRegistry& registry,
                    std::vector<std::string>* dropped)
{
    attributes.Clear();

    archive.BeginObject(std::string_view{});
    std::size_t count = 0;
    archive.BeginArray(std::string_view{ "attributes" }, count);
    for (std::size_t i = 0; i < count; ++i)
    {
        archive.BeginObject(std::string_view{});
        std::string name;
        float base = 0.0f;
        archive.Field(std::string_view{ "name" }, name);
        archive.Field(std::string_view{ "base" }, base);
        archive.End();
        if (!archive.Ok())
            break;

        const AttributeId id = registry.FindAttribute(name);
        if (!id.IsValid())
            continue; // attribute not in this registry's vocabulary: skip

        if (attributes.Has(id) || attributes.Size() < AttributeSet::Capacity)
            attributes.Add(id, base);
        else if (dropped != nullptr)
            dropped->push_back(std::move(name));
    }
    archive.End(); // attributes array
    archive.End(); // root object
    return archive.Ok();
}
